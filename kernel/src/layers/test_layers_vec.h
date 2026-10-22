/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_layers_vec.h — five manifests written by build_system/mk_layers.py
 * (l0 from build_system/layer0.machines; l1 over a 9-byte BOOTX64.EFI
 * "efi bytes" and golden.core; l2 from golden.swarm; l3 evo_comm over one
 * booklet "hello booklet\n"; l4 over one overlay "overlay\n") and the CIDs
 * mk_layers.py printed for them. test_layers.c checks the kernel reads them
 * and names them the same. */
#ifndef ZXV_TEST_LAYERS_VEC_H
#define ZXV_TEST_LAYERS_VEC_H

static const char VEC_L0[] =
    "{\n"
    " \"format\": \"zxv-layer 1\",\n"
    " \"layer_depth\": 0,\n"
    " \"layer_name\": \"HARDWARE_ENVELOPE\",\n"
    " \"parent_layer_cid\": null,\n"
    " \"parent_compat\": \"exact\",\n"
    " \"objects\": [\n"
    "  {\"kind\": \"machine\", \"arch\": \"x86_64\", \"firmware\": \"UEFI\", \"link_address\": "
    "\"0x100000\", \"page_bytes\": 4096, \"sector_bytes\": 512, \"min_ram_mib\": 512},\n"
    "  {\"kind\": \"machine\", \"arch\": \"arm64\", \"firmware\": \"UEFI\", \"link_address\": "
    "\"0x40080000\", \"page_bytes\": 4096, \"sector_bytes\": 512, \"min_ram_mib\": 512}\n"
    " ]\n"
    "}\n";
static const char CID_L0[] = "bafkreicb3bhpw3izymy6bkkgcnzlqrrtiprvdptbviwlobhdwbs62z3ena";

static const char VEC_L1[] =
    "{\n"
    " \"format\": \"zxv-layer 1\",\n"
    " \"layer_depth\": 1,\n"
    " \"layer_name\": \"CORE_BOOT_ENGINE\",\n"
    " \"parent_layer_cid\": \"bafkreicb3bhpw3izymy6bkkgcnzlqrrtiprvdptbviwlobhdwbs62z3ena\",\n"
    " \"parent_compat\": \"exact\",\n"
    " \"objects\": [\n"
    "  {\"kind\": \"boot\", \"arch\": \"x86_64\", \"name\": \"BOOTX64.EFI\", \"bytes\": 9, "
    "\"sha256\": \"b57af4343ba7a27d5120be37a8f6547e2a60312c333f3a4ace26cb600e3816bd\", \"cid\": "
    "\"bafkreifvpl2dio5huj6vcif6g6upmvd6fjqdclbth45evtrgznqa4oawxu\"},\n"
    "  {\"kind\": \"model\", \"arch\": \"all\", \"name\": \"qwen2.5-0.5b-instruct-q4_k_m.gguf\", "
    "\"bytes\": 491400032, \"sha256\": "
    "\"74a4da8c9fdbcd15bd1f6d01d621410d31c6fc00986f5eb687824e7b93d7a9db\"}\n"
    " ]\n"
    "}\n";
static const char CID_L1[] = "bafkreiehadgrtrlpkjkmm75aardweizj6wicpsrdwu63vfl4tpoqcdzcm4";

static const char VEC_L2[] =
    "{\n"
    " \"format\": \"zxv-layer 1\",\n"
    " \"layer_depth\": 2,\n"
    " \"layer_name\": \"SWARM\",\n"
    " \"parent_layer_cid\": \"bafkreiehadgrtrlpkjkmm75aardweizj6wicpsrdwu63vfl4tpoqcdzcm4\",\n"
    " \"parent_compat\": \"exact\",\n"
    " \"objects\": [\n"
    "  {\"kind\": \"model\", \"arch\": \"all\", \"group\": \"REASON\", \"name\": "
    "\"qwen2.5-1.5b-instruct-q4_k_m.gguf\", \"bytes\": 1117320736, \"sha256\": "
    "\"6a1a2eb6d15622bf3c96857206351ba97e1af16c30d7a74ee38970e434e9407e\"}\n"
    " ]\n"
    "}\n";
static const char CID_L2[] = "bafkreiamohmauysssrk42kawicucn3nkyjm5zdirqxqytqb3tgqjhm362i";

static const char VEC_L3[] =
    "{\n"
    " \"format\": \"zxv-layer 1\",\n"
    " \"layer_depth\": 3,\n"
    " \"layer_name\": \"ALEXANDRIA\",\n"
    " \"parent_layer_cid\": \"bafkreiamohmauysssrk42kawicucn3nkyjm5zdirqxqytqb3tgqjhm362i\",\n"
    " \"parent_compat\": \"evo_comm\",\n"
    " \"objects\": [\n"
    "  {\"kind\": \"booklet\", \"arch\": \"all\", \"name\": \"a.txt\", \"bytes\": 14, \"sha256\": "
    "\"c2c0c252aac1465432ef1c4cd67966ddca0c45da74667de141cc50abb00726e9\", \"cid\": "
    "\"bafkreigcydbffkwbizkdf3y4jtlhszw5zigelwtumz66cqomkcv3abzg5e\"}\n"
    " ]\n"
    "}\n";
static const char CID_L3[] = "bafkreif6r4xchbt4jcaubm3675f7vz6syr7zrbdxt34w3uymrnvqswi43u";

static const char VEC_L4[] =
    "{\n"
    " \"format\": \"zxv-layer 1\",\n"
    " \"layer_depth\": 4,\n"
    " \"layer_name\": \"COMMUNITY_OVERLAY\",\n"
    " \"parent_layer_cid\": \"bafkreif6r4xchbt4jcaubm3675f7vz6syr7zrbdxt34w3uymrnvqswi43u\",\n"
    " \"parent_compat\": \"exact\",\n"
    " \"objects\": [\n"
    "  {\"kind\": \"overlay\", \"arch\": \"all\", \"name\": \"o.txt\", \"bytes\": 8, \"sha256\": "
    "\"5971fc5991ae2b514c4e25cbfaa154aa191b4230c4201e33c00da182209f53fd\", \"cid\": "
    "\"bafkreiczoh6ftenofniuytrfzp5kcvfkdenuemgeeapdhqanugbcbh2t7u\"}\n"
    " ]\n"
    "}\n";
static const char CID_L4[] = "bafkreiahzkm6zbgozbtsinn4h5gooft6freafn56fkupcfw435lrbuwr4u";

#endif
