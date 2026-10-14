# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# Generate zt_tts_tables.h for zt_tts.c (T26): the trigonometric tables of the
# integer iSTFT and the default OuteTTS speaker. Every entry is computed with
# 60-digit decimal arithmetic and rounded to nearest, so the file is
# reproducible.
#   ZT_TTS_SIN[i]   = sin(2 pi i / 1024), i = 0..256 (a quarter turn), Q30
#   ZT_TTS_FSIN[j]  = sin(2 pi j / 2^20), j = 0..1023, Q30 (the fine step)
#   ZT_TTS_FCOS[j]  = cos(2 pi j / 2^20), j = 0..1023, Q30
#   ZT_TTS_TW[j]    = cos(2 pi j / 1280), j = 0..320 (a quarter turn), Q30:
#                     the inverse DFT's twiddles and the Hann window
#   ZT_TTS_LN2_Q32  = round(ln 2 * 2^32)
#   ZT_TTS_TURN_K   = round(2^48 / (2 pi)): a Q16 angle times this is the
#                     angle in turns * 2^64 (modulo 2^64)
#   ZT_TTS_SPK_TEXT, ZT_TTS_SPK_AUDIO: llama.cpp's default OuteTTS speaker
#                     (en_male_1 from the OuteTTS repository, as llama.cpp's
#                     tools/tts/tts.cpp held it), in the version 0.3 form.
# Usage: python3 gen_tts_tables.py zt_tts_tables.h
import sys
from decimal import Decimal, getcontext
getcontext().prec = 60


def pi():
    def atan_inv(x):
        x = Decimal(x); t = 1 / x; s = t; n = 1; x2 = x * x
        while True:
            t /= -x2; n += 2; d = t / n
            if abs(d) < Decimal(10) ** -58: return s
            s += d
    return 16 * atan_inv(5) - 4 * atan_inv(239)


PI = pi()


def sin(x):
    s, t, n = Decimal(0), x, 1
    while abs(t) > Decimal(10) ** -58:
        s += t; t = -t * x * x / ((n + 1) * (n + 2)); n += 2
    return s


def cos(x): return sin(PI / 2 - x)


def ln2():
    # ln 2 = 2 atanh(1/3)
    s, t, n = Decimal(0), Decimal(1) / 3, 1
    while t > Decimal(10) ** -58:
        s += t / n; t /= 9; n += 2
    return 2 * s


def rnd(d): return int(d.to_integral_value(rounding='ROUND_HALF_EVEN'))


Q = Decimal(2) ** 30
sin_q = [rnd(sin(2 * PI * i / 1024) * Q) for i in range(257)]
fsin = [rnd(sin(2 * PI * j / 2 ** 20) * Q) for j in range(1024)]
fcos = [rnd(cos(2 * PI * j / 2 ** 20) * Q) for j in range(1024)]
tw = [rnd(cos(2 * PI * j / 1280) * Q) for j in range(321)]

# The default speaker: (word, duration, WavTokenizer codes).
SPEAKER = [
    ('the', '0.08', [257, 740, 636, 913, 788, 1703]),
    ('overall', '0.36', [127, 201, 191, 774, 700, 532, 1056, 557, 798, 298, 1741, 747, 1662, 1617, 1702, 1527, 368, 1588, 1049, 1008, 1625, 747, 1576, 728, 1019, 1696, 1765]),
    ('package', '0.56', [935, 584, 1319, 627, 1016, 1491, 1344, 1117, 1526, 1040, 239, 1435, 951, 498, 723, 1180, 535, 789, 1649, 1637, 78, 465, 1668, 901, 595, 1675, 117, 1009, 1667, 320, 840, 79, 507, 1762, 1508, 1228, 1768, 802, 1450, 1457, 232, 639]),
    ('from', '0.19', [604, 782, 1682, 872, 1532, 1600, 1036, 1761, 647, 1554, 1371, 653, 1595, 950]),
    ('just', '0.25', [1782, 1670, 317, 786, 1748, 631, 599, 1155, 1364, 1524, 36, 1591, 889, 1535, 541, 440, 1532, 50, 870]),
    ('two', '0.24', [1681, 1510, 673, 799, 805, 1342, 330, 519, 62, 640, 1138, 565, 1552, 1497, 1552, 572, 1715, 1732]),
    ('people', '0.39', [593, 274, 136, 740, 691, 633, 1484, 1061, 1138, 1485, 344, 428, 397, 1562, 645, 917, 1035, 1449, 1669, 487, 442, 1484, 1329, 1832, 1704, 600, 761, 653, 269]),
    ('is', '0.16', [566, 583, 1755, 646, 1337, 709, 802, 1008, 485, 1583, 652, 10]),
    ('pretty', '0.32', [1818, 1747, 692, 733, 1010, 534, 406, 1697, 1053, 1521, 1355, 1274, 816, 1398, 211, 1218, 817, 1472, 1703, 686, 13, 822, 445, 1068]),
    ('remarkable', '0.68', [230, 1048, 1705, 355, 706, 1149, 1535, 1787, 1356, 1396, 835, 1583, 486, 1249, 286, 937, 1076, 1150, 614, 42, 1058, 705, 681, 798, 934, 490, 514, 1399, 572, 1446, 1703, 1346, 1040, 1426, 1304, 664, 171, 1530, 625, 64, 1708, 1830, 1030, 443, 1509, 1063, 1605, 1785, 721, 1440, 923]),
    ('sure', '0.36', [792, 1780, 923, 1640, 265, 261, 1525, 567, 1491, 1250, 1730, 362, 919, 1766, 543, 1, 333, 113, 970, 252, 1606, 133, 302, 1810, 1046, 1190, 1675]),
    ('i', '0.08', [123, 439, 1074, 705, 1799, 637]),
    ('have', '0.16', [1509, 599, 518, 1170, 552, 1029, 1267, 864, 419, 143, 1061, 0]),
    ('some', '0.16', [619, 400, 1270, 62, 1370, 1832, 917, 1661, 167, 269, 1366, 1508]),
    ('critiques', '0.60', [559, 584, 1163, 1129, 1313, 1728, 721, 1146, 1093, 577, 928, 27, 630, 1080, 1346, 1337, 320, 1382, 1175, 1682, 1556, 990, 1683, 860, 1721, 110, 786, 376, 1085, 756, 1523, 234, 1334, 1506, 1578, 659, 612, 1108, 1466, 1647, 308, 1470, 746, 556, 1061]),
    ('about', '0.29', [26, 1649, 545, 1367, 1263, 1728, 450, 859, 1434, 497, 1220, 1285, 179, 755, 1154, 779, 179, 1229, 1213, 922, 1774, 1408]),
    ('some', '0.23', [986, 28, 1649, 778, 858, 1519, 1, 18, 26, 1042, 1174, 1309, 1499, 1712, 1692, 1516, 1574]),
    ('of', '0.07', [197, 716, 1039, 1662, 64]),
    ('the', '0.08', [1811, 1568, 569, 886, 1025, 1374]),
    ('gameplay', '0.48', [1269, 1092, 933, 1362, 1762, 1700, 1675, 215, 781, 1086, 461, 838, 1022, 759, 649, 1416, 1004, 551, 909, 787, 343, 830, 1391, 1040, 1622, 1779, 1360, 1231, 1187, 1317, 76, 997, 989, 978, 737, 189]),
    ('aspects', '0.56', [1423, 797, 1316, 1222, 147, 719, 1347, 386, 1390, 1558, 154, 440, 634, 592, 1097, 1718, 712, 763, 1118, 1721, 1311, 868, 580, 362, 1435, 868, 247, 221, 886, 1145, 1274, 1284, 457, 1043, 1459, 1818, 62, 599, 1035, 62, 1649, 778]),
    ('but', '0.20', [780, 1825, 1681, 1007, 861, 710, 702, 939, 1669, 1491, 613, 1739, 823, 1469, 648]),
    ('its', '0.09', [92, 688, 1623, 962, 1670, 527, 599]),
    ('still', '0.27', [636, 10, 1217, 344, 713, 957, 823, 154, 1649, 1286, 508, 214, 1760, 1250, 456, 1352, 1368, 921, 615, 5]),
    ('really', '0.36', [55, 420, 1008, 1659, 27, 644, 1266, 617, 761, 1712, 109, 1465, 1587, 503, 1541, 619, 197, 1019, 817, 269, 377, 362, 1381, 507, 1488, 4, 1695]),
    ('enjoyable', '0.49', [678, 501, 864, 319, 288, 1472, 1341, 686, 562, 1463, 619, 1563, 471, 911, 730, 1811, 1006, 520, 861, 1274, 125, 1431, 638, 621, 153, 876, 1770, 437, 987, 1653, 1109, 898, 1285, 80, 593, 1709, 843]),
    ('and', '0.15', [1285, 987, 303, 1037, 730, 1164, 502, 120, 1737, 1655, 1318]),
    ('it', '0.09', [848, 1366, 395, 1601, 1513, 593, 1302]),
    ('looks', '0.27', [1281, 1266, 1755, 572, 248, 1751, 1257, 695, 1380, 457, 659, 585, 1315, 1105, 1776, 736, 24, 736, 654, 1027]),
    ('lovely', '0.56', [634, 596, 1766, 1556, 1306, 1285, 1481, 1721, 1123, 438, 1246, 1251, 795, 659, 1381, 1658, 217, 1772, 562, 952, 107, 1129, 1112, 467, 550, 1079, 840, 1615, 1469, 1380, 168, 917, 836, 1827, 437, 583, 67, 595, 1087, 1646, 1493, 1677]),
]
spk_text = '<|text_start|>' + ''.join(w + '<|space|>' for w, _, _ in SPEAKER)
spk_audio = '<|audio_start|>\n' + '\n'.join(
    w + '<|t_' + d + '|>' + ''.join('<|%d|>' % c for c in cs) + '<|space|>' for w, d, cs in SPEAKER)
out = ['/* zt_tts_tables.h — generated by gen_tts_tables.py. Do not edit. */', '#include <stdint.h>']


def emit(name, vals, what):
    out.append('/* %s */' % what)
    out.append('static const int32_t %s[%d] = {' % (name, len(vals)))
    for i in range(0, len(vals), 8): out.append('    ' + ', '.join(str(v) for v in vals[i:i + 8]) + ',')
    out.append('};')


def cstr(name, s):
    out.append('static const char %s[] =' % name)
    parts = s.split('\n')
    for i, p in enumerate(parts):
        p = p + ('\\n' if i + 1 < len(parts) else '')
        for k in range(0, len(p), 88):
            out.append('    "%s"' % p[k:k + 88])
    out[-1] += ';'


emit('ZT_TTS_SIN', sin_q, 'sin(2 pi i / 1024) in Q30, a quarter turn')
emit('ZT_TTS_FSIN', fsin, 'sin(2 pi j / 2^20) in Q30')
emit('ZT_TTS_FCOS', fcos, 'cos(2 pi j / 2^20) in Q30')
emit('ZT_TTS_TW', tw, 'cos(2 pi j / 1280) in Q30, a quarter turn')
out.append('#define ZT_TTS_LN2_Q32 %du' % rnd(ln2() * 2 ** 32))
out.append('#define ZT_TTS_TURN_K  %dull' % rnd(Decimal(2) ** 48 / (2 * PI)))
cstr('ZT_TTS_SPK_TEXT', spk_text)
cstr('ZT_TTS_SPK_AUDIO', spk_audio)
open(sys.argv[1], 'w').write('\n'.join(out) + '\n')
