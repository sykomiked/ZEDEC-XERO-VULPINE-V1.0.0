// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import Foundation
import XCTest

@testable import ZXVRPC

final class MoneyTests: XCTestCase {
    func testParsesWholeAndDecimals() {
        XCTAssertEqual(try Money.parse("125.50", currency: "USD").get(), 12550)
        XCTAssertEqual(try Money.parse("125.5", currency: "USD").get(), 12550)
        XCTAssertEqual(try Money.parse("125", currency: "USD").get(), 12500)
        XCTAssertEqual(try Money.parse("1,250.05", currency: "EUR").get(), 125005)
        XCTAssertEqual(try Money.parse(" 0.01 ", currency: "USD").get(), 1)
        XCTAssertEqual(try Money.parse("1.500", currency: "USD").get(), 150)
    }

    func testRefusesFractionsOfAMinorUnit() {
        XCTAssertEqual(Money.parse("1.505", currency: "USD"), .failure(.fraction(allowed: 2)))
        XCTAssertEqual(Money.parse("100.5", currency: "JPY"), .failure(.fraction(allowed: 0)))
        XCTAssertEqual(Money.parse("1.0005", currency: "KWD"), .failure(.fraction(allowed: 3)))
    }

    func testRefusesNonNumbers() {
        for t in ["", "abc", "-5", "+5", "1e3", "1.", ".5", "1.2.3", "1 000", "١٢"] {
            XCTAssertThrowsError(try Money.parse(t, currency: "USD").get(), t)
        }
        XCTAssertEqual(Money.parse("0.00", currency: "USD"), .failure(.zero))
        XCTAssertEqual(Money.parse("99999999999999999999", currency: "USD"), .failure(.tooLarge))
    }

    func testCurrencyExponents() {
        XCTAssertEqual(try Money.parse("500", currency: "JPY").get(), 500)
        XCTAssertEqual(try Money.parse("1.234", currency: "BHD").get(), 1234)
        XCTAssertEqual(Money.exponent("vfv"), 2)
    }

    func testFormats() {
        XCTAssertEqual(Money.format(124_800_000, currency: "USD"), "1,248,000.00 USD")
        XCTAssertEqual(Money.format(5, currency: "USD"), "0.05 USD")
        XCTAssertEqual(Money.format(-26_000, currency: "USD"), "\u{2212}260.00 USD")
        XCTAssertEqual(Money.format(1234, currency: "JPY", withCode: false), "1,234")
        XCTAssertEqual(Money.format(Int64.min, currency: "USD", withCode: false), "\u{2212}92,233,720,368,547,758.08")
    }

    func testParamSwitchesToStringAbove53Bits() {
        XCTAssertEqual(Money.param(12550), .int(12550))
        XCTAssertEqual(Money.param(9_007_199_254_740_992), .string("9007199254740992"))
    }
}

final class NetCommandTests: XCTestCase {
    let node = String(repeating: "ab", count: 32)

    func testBuildsTheDaemonsLines() throws {
        XCTAssertEqual(try NetCommand.mode(.lan).text(), "lan")
        XCTAssertEqual(try NetCommand.peer("192.168.1.20:8723").text(), "peer 192.168.1.20:8723")
        XCTAssertEqual(try NetCommand.share(compute: 8, storage: 0).text(), "share 8 0")
        XCTAssertEqual(try NetCommand.want(.storage, units: 20, maxPrice: 1500).text(), "want storage 20 1500")
        XCTAssertEqual(try NetCommand.sell(.compute, units: 40, price: 2400, nodeID: node.uppercased()).text(),
                       "sell compute 40 2400 " + node)
    }

    func testRefusesBadInput() {
        for a in ["192.168.1.20", "192.168.1:80", "256.1.1.1:80", "1.2.3.4:0", "1.2.3.4:70000", "h:80", "1.2.3.4:8a"] {
            XCTAssertThrowsError(try NetCommand.peer(a).text(), a)
        }
        XCTAssertThrowsError(try NetCommand.sell(.compute, units: 1, price: 1, nodeID: "7f3a91c2e04b5d18").text())
        XCTAssertThrowsError(try NetCommand.want(.compute, units: 0, maxPrice: 5).text())
    }
}

final class DecodeTests: XCTestCase {
    func testErrorReplyCarriesTheLibzxvStatus() {
        let line = Data(#"{"jsonrpc":"2.0","id":3,"error":{"code":-32000,"message":"refused","data":{"status":-6,"name":"ZXV_E_FUNDS","class":"REFUSED"}}}"#.utf8)
        XCTAssertThrowsError(try ZXVDClient.decodeReply(line, id: 3, as: Posted.self, decoder: ZXVDClient.typedDecoder)) { e in
            guard case ZXVDError.rpc(let r) = e else { return XCTFail("\(e)") }
            XCTAssertEqual(r.code, -32000)
            XCTAssertEqual(r.statusName, "ZXV_E_FUNDS")
            XCTAssertEqual(r.data?.kind, "REFUSED")
        }
    }

    func testMismatchedIdIsRefused() {
        let line = Data(#"{"jsonrpc":"2.0","id":4,"result":{"ok":true}}"#.utf8)
        XCTAssertThrowsError(try ZXVDClient.decodeReply(line, id: 5, as: Checked.self, decoder: ZXVDClient.typedDecoder))
    }

    func testBalanceStringsAreExact() throws {
        let line = Data(#"{"jsonrpc":"2.0","id":1,"result":{"debit_minor":"9223372036854775807","credit_minor":"0","equity_minor":"-5"}}"#.utf8)
        let b = try ZXVDClient.decodeReply(line, id: 1, as: Balance.self, decoder: ZXVDClient.typedDecoder)
        XCTAssertEqual(b.debit, Int64.max)
        XCTAssertEqual(b.equity, -5)
    }

    func testUntypedKeepsKeysAsSent() throws {
        let line = Data(#"{"jsonrpc":"2.0","id":1,"result":{"debit_minor":"1","n":2}}"#.utf8)
        let j = try ZXVDClient.decodeReply(line, id: 1, as: JSON.self, decoder: JSONDecoder())
        XCTAssertEqual(j["debit_minor"], .string("1"))
        XCTAssertEqual(j["n"]?.int, 2)
    }

    func testNoDaemonIsReported() {
        let c = ZXVDClient(socketPath: NSTemporaryDirectory() + "no-such-zxvd.sock", timeoutMs: 500)
        XCTAssertThrowsError(try c.call("rpc.discover")) { e in
            guard case ZXVDError.noDaemon = e else { return XCTFail("\(e)") }
        }
    }

    func testPeerAndOfferRowsDecode() throws {
        let line = Data(#"{"jsonrpc":"2.0","id":3,"result":{"peers":[{"id":"ab","addr":"127.0.0.1:8723","bucket":4,"ubh":false,"last_seen_ms":"1700000000000","in_book":true,"member":false,"held":"0","receivable":"9","social":"0","pending":1}],"count":1}}"#.utf8)
        let p = try ZXVDClient.decodeReply(line, id: 3, as: Peers.self, decoder: ZXVDClient.typedDecoder)
        XCTAssertEqual(p.peers.first?.inBook, true)
        XCTAssertEqual(p.peers.first?.receivable, "9")
        XCTAssertEqual(p.peers.first?.lastSeen, Date(timeIntervalSince1970: 1_700_000_000))
        let o = Data(#"{"jsonrpc":"2.0","id":4,"result":{"share":{"compute":"5","storage":"0"},"wants":[{"slot":0,"resource":"compute","units_left":"6","max_price":"100"}],"outstanding":[{"role":"sell","peer":"cd","units":"4","seq":"1","tries":1,"want":-1}]}}"#.utf8)
        let off = try ZXVDClient.decodeReply(o, id: 4, as: Offers.self, decoder: ZXVDClient.typedDecoder)
        XCTAssertEqual(off.wants.first?.unitsLeft, "6")
        XCTAssertEqual(off.outstanding.first?.want, -1)
    }

    func testChatRecordsDecode() throws {
        let line = Data(#"{"jsonrpc":"2.0","id":5,"result":{"posts":[{"id":"aa","author":"bb","mine":false,"kind":"post","lamport":"7","wall_ms":"1700000000000","text":"hi","reply_to":null,"replies":2,"reactions":[{"emoji":"ok","count":3,"mine":true}]}]}}"#.utf8)
        let f = try ZXVDClient.decodeReply(line, id: 5, as: ChatFeed.self, decoder: ZXVDClient.typedDecoder)
        XCTAssertEqual(f.posts.first?.wallMs, "1700000000000")
        XCTAssertEqual(f.posts.first?.date, Date(timeIntervalSince1970: 1_700_000_000))
        XCTAssertNil(f.posts.first?.replyTo)
        XCTAssertEqual(f.posts.first?.reactions.first?.count, 3)
        let none = Data(#"{"jsonrpc":"2.0","id":6,"result":{"node_id":"cc","saved":false,"records":0,"following":[]}}"#.utf8)
        let me = try ZXVDClient.decodeReply(none, id: 6, as: ChatMe.self, decoder: ZXVDClient.typedDecoder)
        XCTAssertEqual(me.nodeId, "cc")
        XCTAssertFalse(me.saved)
    }

    func testBooksFileDefault() {
        XCTAssertTrue(ZXVDLocation.defaultBooksPath.hasSuffix("books.zxvb"))
    }
}
