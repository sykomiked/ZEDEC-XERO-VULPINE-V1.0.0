/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_rpc_win.c — zxvd's control socket on Windows (zxvd_rpc.h T5).
 *
 * Windows 10 1803 and later have stream sockets of family AF_UNIX in
 * Winsock, bound to a path in the file system. They work with select(), so
 * zxvd's one loop serves them beside the window's TCP listener, exactly as
 * on Linux and macOS; only the permission model differs:
 *
 *   folder   created with a protected DACL that lets only our user in. An
 *            existing folder must not be a link or junction, must be owned
 *            by our user (or by Administrators or SYSTEM: an elevated
 *            administrator's new folders are), and its DACL may grant
 *            write to nobody but our user, SYSTEM and Administrators (the
 *            parties that are root on Windows anyway). CREATOR OWNER
 *            becomes us for what we create.
 *   socket   a stale socket file is replaced only when nothing answers on
 *            it; anything else at the path is refused. After bind, the file
 *            gets a protected owner-only DACL: connecting needs write access
 *            to it, so nobody else can connect.
 *   peer     SIO_AF_UNIX_GETPEERPID names the peer process; its token's user
 *            SID must equal ours, else the connection is closed before a
 *            byte is read.
 *
 * Framing, limits and the methods are zxvd_rpc.c's (zxvd_rpc_handle_line).
 * The caller has already run WSAStartup (zxv_host.c main). */
#if defined(_WIN32)
#    ifndef _WIN32_WINNT
#        define _WIN32_WINNT 0x0A00
#    endif
#    define WIN32_LEAN_AND_MEAN
#    include <winsock2.h>
#    include <ws2tcpip.h>
#    include <windows.h>
#    include <afunix.h>
#    include <aclapi.h>
#    include <stdbool.h>
#    include <stdio.h>
#    include <stdlib.h>
#    include <string.h>

#    include "zxvd_rpc.h"

#    ifndef IO_REPARSE_TAG_AF_UNIX
#        define IO_REPARSE_TAG_AF_UNIX 0x80000023u
#    endif

typedef struct {
    SOCKET fd;                      /* INVALID_SOCKET when free */
    char in[ZXVD_RPC_MAX_LINE + 2]; /* a full-length line, its LF, and a NUL */
    size_t n;
    uint64_t last_ms;
} client_t;

static SOCKET LS = INVALID_SOCKET;
static char PATH_[sizeof(((SOCKADDR_UN *) 0)->sun_path)];
static client_t CL[ZXVD_RPC_MAX_CLIENTS];
static PSID ME; /* our user's SID (inside ME_TOK) */
static TOKEN_USER *ME_TOK;

/* ---------- who we are, who the peer is ---------- */

static TOKEN_USER *token_user(HANDLE proc)
{
    HANDLE tok;
    if (!OpenProcessToken(proc, TOKEN_QUERY, &tok)) return NULL;
    DWORD need = 0;
    GetTokenInformation(tok, TokenUser, NULL, 0, &need);
    TOKEN_USER *tu = need ? malloc(need) : NULL;
    if (tu && !GetTokenInformation(tok, TokenUser, tu, need, &need)) {
        free(tu);
        tu = NULL;
    }
    CloseHandle(tok);
    return tu;
}

static bool same_user(SOCKET fd)
{
    ULONG pid = 0;
    DWORD got = 0;
    if (WSAIoctl(fd, SIO_AF_UNIX_GETPEERPID, NULL, 0, &pid, sizeof pid, &got, NULL, NULL) != 0 ||
        got != sizeof pid || pid == 0)
        return false;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return false;
    TOKEN_USER *tu = token_user(p);
    CloseHandle(p);
    bool ok = tu && EqualSid(tu->User.Sid, ME);
    free(tu);
    return ok;
}

/* ---------- ACLs ---------- */

/* A protected DACL with one ACE: full control for our user. `inherit`
 * passes it to what is created inside (a folder's case). */
static PACL owner_only_acl(bool inherit)
{
    EXPLICIT_ACCESS_A ea;
    memset(&ea, 0, sizeof ea);
    ea.grfAccessPermissions = GENERIC_ALL;
    ea.grfAccessMode = SET_ACCESS;
    ea.grfInheritance = inherit ? (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE) : NO_INHERITANCE;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType = TRUSTEE_IS_USER;
    ea.Trustee.ptstrName = (LPSTR) ME;
    PACL acl = NULL;
    if (SetEntriesInAclA(1, &ea, NULL, &acl) != ERROR_SUCCESS) return NULL;
    return acl;
}

static bool set_owner_only(const char *path, bool inherit)
{
    PACL acl = owner_only_acl(inherit);
    if (!acl) return false;
    DWORD e = SetNamedSecurityInfoA((LPSTR) path, SE_FILE_OBJECT,
                                    DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                    NULL, NULL, acl, NULL);
    LocalFree(acl);
    return e == ERROR_SUCCESS;
}

static bool trusted_sid(PSID sid)
{
    if (EqualSid(sid, ME)) return true;
    static const WELL_KNOWN_SID_TYPE OK[] = {WinLocalSystemSid, WinBuiltinAdministratorsSid,
                                             WinCreatorOwnerSid};
    for (size_t i = 0; i < sizeof OK / sizeof OK[0]; i++)
        if (IsWellKnownSid(sid, OK[i])) return true;
    return false;
}

#    define WRITE_RIGHTS                                                                           \
        (FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_DELETE_CHILD | FILE_WRITE_ATTRIBUTES |          \
         FILE_WRITE_EA | DELETE | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL)

/* An existing folder: ours, not a link, and writable by nobody untrusted
 * (including through ACEs its children would inherit). */
static bool dir_is_safe(const char *dir)
{
    DWORD at = GetFileAttributesA(dir);
    if (at == INVALID_FILE_ATTRIBUTES || !(at & FILE_ATTRIBUTE_DIRECTORY) ||
        (at & FILE_ATTRIBUTE_REPARSE_POINT))
        return false;
    PSID owner = NULL;
    PACL dacl = NULL;
    PSECURITY_DESCRIPTOR sd = NULL;
    if (GetNamedSecurityInfoA(dir, SE_FILE_OBJECT,
                              OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, NULL,
                              &dacl, NULL, &sd) != ERROR_SUCCESS)
        return false;
    /* an elevated administrator's folders are owned by Administrators */
    bool ok = owner && trusted_sid(owner) && dacl != NULL; /* a NULL DACL lets everyone in */
    for (DWORD i = 0; ok && i < dacl->AceCount; i++) {
        ACE_HEADER *h;
        if (!GetAce(dacl, i, (void **) &h)) {
            ok = false;
            break;
        }
        if (h->AceType != ACCESS_ALLOWED_ACE_TYPE) {
            /* deny ACEs only narrow access; any other allow form is refused */
            if (h->AceType != ACCESS_DENIED_ACE_TYPE) ok = false;
            continue;
        }
        ACCESS_ALLOWED_ACE *a = (ACCESS_ALLOWED_ACE *) h;
        if ((a->Mask & WRITE_RIGHTS) && !trusted_sid((PSID) &a->SidStart)) ok = false;
    }
    LocalFree(sd);
    return ok;
}

/* The socket's folder: created owner-only if missing, else checked. A bare
 * file name is the current folder, the owner's own choice, and is not
 * checked (as on POSIX). */
static bool prepare_dir(const char *path)
{
    char dir[sizeof PATH_];
    snprintf(dir, sizeof dir, "%s", path);
    char *s1 = strrchr(dir, '\\'), *s2 = strrchr(dir, '/');
    char *slash = s1 > s2 ? s1 : s2;
    if (!slash) return true;
    if (slash == dir || (slash == dir + 2 && dir[1] == ':')) return false; /* a drive root */
    *slash = 0;
    PACL acl = owner_only_acl(true);
    if (!acl) return false;
    SECURITY_DESCRIPTOR sd;
    SECURITY_ATTRIBUTES sa = {sizeof sa, &sd, FALSE};
    bool made = InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION) &&
                SetSecurityDescriptorDacl(&sd, TRUE, acl, FALSE) &&
                SetSecurityDescriptorControl(&sd, SE_DACL_PROTECTED, SE_DACL_PROTECTED) &&
                CreateDirectoryA(dir, &sa);
    LocalFree(acl);
    return made || dir_is_safe(dir);
}

/* What is at the path: 0 nothing, 1 a socket file, -1 something else. */
static int what_is_there(const char *path)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(path, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES ? 0 : -1;
    FindClose(h);
    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
        fd.dwReserved0 == IO_REPARSE_TAG_AF_UNIX)
        return 1;
    return -1;
}

static SOCKET unix_socket(void)
{
    /* never inherited by a child process (the model fetcher, a browser) */
    return WSASocketW(AF_UNIX, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_NO_HANDLE_INHERIT);
}

static void forget_me(void)
{
    free(ME_TOK);
    ME_TOK = NULL;
    ME = NULL;
}

/* ---------- open, serve, close ---------- */

int zxvd_rpc_open(const char *path, const zxvd_host_ops_t *ops)
{
    SOCKADDR_UN a;
    if (!path || strlen(path) >= sizeof a.sun_path) {
        fprintf(stderr, "zxvd: --rpc path too long (max %u bytes)\n",
                (unsigned) (sizeof a.sun_path - 1));
        return -1;
    }
    if (!(ME_TOK = token_user(GetCurrentProcess()))) {
        fprintf(stderr, "zxvd: cannot read this process's user\n");
        return -1;
    }
    ME = ME_TOK->User.Sid;
    if (!prepare_dir(path)) {
        fprintf(stderr,
                "zxvd: the folder of %s is not usable (missing, not ours, or writable by others)\n",
                path);
        forget_me();
        return -1;
    }
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    memcpy(a.sun_path, path, strlen(path) + 1);
    int there = what_is_there(path);
    if (there < 0) {
        fprintf(stderr, "zxvd: %s exists and is not a socket; refusing to replace it\n", path);
        forget_me();
        return -1;
    }
    if (there > 0) {
        SOCKET probe = unix_socket();
        bool live =
            probe != INVALID_SOCKET && connect(probe, (struct sockaddr *) &a, sizeof a) == 0;
        if (probe != INVALID_SOCKET) closesocket(probe);
        if (live) {
            fprintf(stderr, "zxvd: another zxvd answers on %s\n", path);
            forget_me();
            return -1;
        }
        DeleteFileA(path); /* stale */
    }
    if (zxvd_rpc_books_open() != 0) {
        fprintf(stderr, "zxvd: libzxv context could not be created\n");
        forget_me();
        return -1;
    }
    SOCKET fd = unix_socket();
    if (fd == INVALID_SOCKET) {
        fprintf(stderr, "zxvd: no AF_UNIX sockets here (Windows 10 1803 or later is needed)\n");
        zxvd_rpc_books_close();
        forget_me();
        return -1;
    }
    if (bind(fd, (struct sockaddr *) &a, sizeof a) != 0 || !set_owner_only(path, false) ||
        listen(fd, 8) != 0) {
        fprintf(stderr, "zxvd: cannot listen on %s (error %d)\n", path, WSAGetLastError());
        closesocket(fd);
        DeleteFileA(path);
        zxvd_rpc_books_close();
        forget_me();
        return -1;
    }
    LS = fd;
    snprintf(PATH_, sizeof PATH_, "%s", path);
    for (int i = 0; i < ZXVD_RPC_MAX_CLIENTS; i++) CL[i].fd = INVALID_SOCKET;
    zxvd_rpc_set_ops(ops);
    return 0;
}

int zxvd_rpc_fds(void *set)
{
    fd_set *s = set;
    if (LS == INVALID_SOCKET) return -1;
    FD_SET(LS, s);
    for (int i = 0; i < ZXVD_RPC_MAX_CLIENTS; i++)
        if (CL[i].fd != INVALID_SOCKET) FD_SET(CL[i].fd, s);
    return 0; /* Windows' select ignores its first argument */
}

static void drop(client_t *c)
{
    closesocket(c->fd);
    c->fd = INVALID_SOCKET;
    c->n = 0;
}

/* send all of it, or give up on this client (2 s send timeout) */
static bool send_line(client_t *c, const zxvd_buf_t *b)
{
    size_t off = 0;
    while (off < b->len) {
        size_t chunk = b->len - off > 65536 ? 65536 : b->len - off;
        int k = send(c->fd, b->p + off, (int) chunk, 0);
        if (k <= 0) return false;
        off += (size_t) k;
    }
    return send(c->fd, "\n", 1, 0) == 1;
}

static void serve(client_t *c, uint64_t now_ms)
{
    int k = recv(c->fd, c->in + c->n, (int) (sizeof c->in - 1 - c->n), 0);
    if (k <= 0) {
        drop(c);
        return;
    }
    c->n += (size_t) k;
    c->last_ms = now_ms;
    zxvd_buf_t out = {0};
    for (;;) {
        char *nl = memchr(c->in, '\n', c->n);
        if (!nl) break;
        size_t ll = (size_t) (nl - c->in);
        size_t body = ll && c->in[ll - 1] == '\r' ? ll - 1 : ll;
        bool reply = body ? zxvd_rpc_handle_line(c->in, body, &out) : false;
        if (reply && (out.oom || !send_line(c, &out))) {
            drop(c);
            free(out.p);
            return;
        }
        memmove(c->in, nl + 1, c->n - ll - 1);
        c->n -= ll + 1;
    }
    free(out.p);
    if (c->n >= sizeof c->in - 1) drop(c); /* T3: line too long */
}

void zxvd_rpc_service(void *set, uint64_t now_ms)
{
    fd_set *s = set;
    if (LS == INVALID_SOCKET) return;
    for (int i = 0; i < ZXVD_RPC_MAX_CLIENTS; i++) {
        client_t *c = &CL[i];
        if (c->fd == INVALID_SOCKET) continue;
        if (FD_ISSET(c->fd, s))
            serve(c, now_ms);
        else if (now_ms - c->last_ms > ZXVD_RPC_IDLE_MS)
            drop(c); /* T4 */
    }
    if (FD_ISSET(LS, s)) {
        SOCKET fd = accept(LS, NULL, NULL);
        if (fd == INVALID_SOCKET) return;
        SetHandleInformation((HANDLE) fd, HANDLE_FLAG_INHERIT, 0);
        if (!same_user(fd)) { /* T2 */
            closesocket(fd);
            return;
        }
        DWORD to = 2000;
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *) &to, sizeof to);
        for (int i = 0; i < ZXVD_RPC_MAX_CLIENTS; i++)
            if (CL[i].fd == INVALID_SOCKET) {
                CL[i].fd = fd;
                CL[i].n = 0;
                CL[i].last_ms = now_ms;
                return;
            }
        closesocket(fd); /* T4: full */
    }
}

void zxvd_rpc_close(void)
{
    if (LS != INVALID_SOCKET) { /* CL[] is only set up once open succeeded */
        for (int i = 0; i < ZXVD_RPC_MAX_CLIENTS; i++)
            if (CL[i].fd != INVALID_SOCKET) drop(&CL[i]);
        closesocket(LS);
        DeleteFileA(PATH_);
        LS = INVALID_SOCKET;
    }
    zxvd_rpc_books_close();
    forget_me();
}
#else
/* POSIX builds use the socket in zxvd_rpc.c; this file is Windows only. */
typedef int zxvd_rpc_win_unused;
#endif
