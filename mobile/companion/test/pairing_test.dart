// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'package:flutter_test/flutter_test.dart';
import 'package:zxv_companion/src/pairing.dart';

const id = '00112233445566778899aabbccddeeff';
final key = 'ab' * 32;

void main() {
  test('a pairing link round-trips', () {
    final p = Pairing.parse(' zxv-phone:1?h=192.168.1.9&p=47410&id=$id&k=$key&n=My%20Pixel ');
    expect(p.host, '192.168.1.9');
    expect(p.port, 47410);
    expect(p.idHex, id);
    expect(p.key.length, 32);
    expect(p.name, 'My Pixel');
    expect(Pairing.parse(p.toLink()).toLink(), p.toLink());
    final q = p.withHost('10.0.0.2', 5000);
    expect(q.host, '10.0.0.2');
    expect(q.idHex, id);
  });

  test('bad links say what is wrong', () {
    String msg(String t) {
      try {
        Pairing.parse(t);
        return '';
      } on FormatException catch (e) {
        return e.message;
      }
    }

    expect(msg('https://example.com'), contains('Not a ZXV pairing link'));
    expect(msg('zxv-phone:1?p=1&id=$id&k=$key'), contains('address'));
    expect(msg('zxv-phone:1?h=a&p=0&id=$id&k=$key'), contains('port'));
    expect(msg('zxv-phone:1?h=a&p=1&id=00&k=$key'), contains('phone id'));
    expect(msg('zxv-phone:1?h=a&p=1&id=$id&k=${'AB' * 32}'), contains('key'));
  });
}
