import Compression
import Foundation

/// EPUB import end to end: hand-built stored and deflated archives, markup
/// clean-up, passage cutting limits, library round trip and hostile inputs.
/// A small hand-built EPUB (stored and deflated entries) shared by the smokes.
struct TypingTestSmokeFixture {
    let epub: Data
    let container: String
    let zip: ([(String, Data, Bool)]) -> Data
    let le: (Int, Int) -> [UInt8]
}

func typingTestSmokeFixture() -> TypingTestSmokeFixture {
    func crc(_ bytes: [UInt8]) -> UInt32 {
        var table = [UInt32](repeating: 0, count: 256)
        for i in 0..<256 { var c = UInt32(i); for _ in 0..<8 { c = c & 1 == 1 ? 0xEDB88320 ^ (c >> 1) : c >> 1 }; table[i] = c }
        return ~bytes.reduce(UInt32.max) { table[Int(($0 ^ UInt32($1)) & 0xFF)] ^ ($0 >> 8) }
    }
    func le(_ value: Int, _ width: Int) -> [UInt8] { (0..<width).map { UInt8((value >> (8 * $0)) & 0xFF) } }
    func zip(_ files: [(String, Data, Bool)]) -> Data {
        var out: [UInt8] = [], directory: [UInt8] = []
        for (name, data, deflate) in files {
            let raw = [UInt8](data)
            var body = raw
            var method = 0
            if deflate, !raw.isEmpty {
                var buffer = [UInt8](repeating: 0, count: raw.count + 1024)
                let size = raw.withUnsafeBufferPointer { source in
                    buffer.withUnsafeMutableBufferPointer { compression_encode_buffer($0.baseAddress!, $0.count, source.baseAddress!, raw.count, nil, COMPRESSION_ZLIB) }
                }
                body = Array(buffer[..<size]); method = 8
            }
            let nameBytes = [UInt8](name.utf8)
            let offset = out.count
            out += le(0x0403_4B50, 4) + le(20, 2) + le(0x800, 2) + le(method, 2) + le(0, 4) + le(Int(crc(raw)), 4)
                + le(body.count, 4) + le(raw.count, 4) + le(nameBytes.count, 2) + le(0, 2) + nameBytes + body
            directory += le(0x0201_4B50, 4) + le(20, 2) + le(20, 2) + le(0x800, 2) + le(method, 2) + le(0, 4) + le(Int(crc(raw)), 4)
                + le(body.count, 4) + le(raw.count, 4) + le(nameBytes.count, 2) + le(0, 2) + le(0, 2) + le(0, 2) + le(0, 2)
                + le(0, 4) + le(offset, 4) + nameBytes
        }
        let start = out.count
        out += directory + le(0x0605_4B50, 4) + le(0, 2) + le(0, 2) + le(files.count, 2) + le(files.count, 2)
            + le(directory.count, 4) + le(start, 4) + le(0, 2)
        return Data(out)
    }
    func xhtml(_ body: String) -> Data {
        Data("<?xml version=\"1.0\"?><html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>ignored head title</title><style>p{}</style></head><body>\(body)</body></html>".utf8)
    }

    let chinese = (1...40).map { "第\($0)段：清晨的街道很安静，<em>老板</em>把热气腾腾的豆浆放在柜台上。赶早班的人快步走来！" }
    let chapterOne = "<h1>第一章 清晨</h1>" + chinese.map { "<p>\n  \($0)\n</p>" }.joined()
        + "<script>var secret = 'SCRIPTTEXT';</script><!-- hidden comment --><p>汉<ruby>字<rt>han</rt></ruby>&amp;&#x4E00;&nbsp;&mdash;结束了这一章的全部内容。</p>"
        + "<p>★ ※ 请参阅附录[12]，详见 http://example.com/x 或写信 mail@example.com ，，，。。。 </p><p>* * * ---- 12</p>"
    let english = (1...30).map { "<p>Sentence number \($0) walks quietly past the bakery. Dr. Smith waved at the baker, who smiled &amp; nodded.</p>" }.joined()
    let container = "<?xml version=\"1.0\"?><container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\"><rootfiles><rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/></rootfiles></container>"
    let opf = """
    <?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="3.0"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>测试之书</dc:title><dc:creator>某作者</dc:creator><dc:language>zh</dc:language></metadata>
    <manifest><item id="cover" href="Text/cover.xhtml" media-type="application/xhtml+xml"/>
    <item id="c1" href="Text/ch%2001.xhtml" media-type="application/xhtml+xml"/><item id="c2" href="Text/ch2.xhtml" media-type="application/xhtml+xml"/>
    <item id="toc" href="Text/toc2.xhtml" media-type="application/xhtml+xml"/><item id="note" href="Text/note.xhtml" media-type="application/xhtml+xml"/><item id="css" href="s.css" media-type="text/css"/></manifest>
    <spine><itemref idref="cover"/><itemref idref="c1"/><itemref idref="toc"/><itemref idref="c2"/><itemref idref="note" linear="no"/></spine></package>
    """
    let epub = zip([
        ("mimetype", Data("application/epub+zip".utf8), false),
        ("META-INF/container.xml", Data(container.utf8), true),
        ("OEBPS/content.opf", Data(opf.utf8), true),
        ("OEBPS/Text/cover.xhtml", xhtml("<p>封面</p>"), false),
        ("OEBPS/Text/ch 01.xhtml", xhtml(chapterOne), true),
        ("OEBPS/Text/ch2.xhtml", xhtml("<h2>Chapter Two</h2>" + english), true),
        ("OEBPS/Text/toc2.xhtml", xhtml("<h1>目录</h1>" + (1...14).map { "<p>第\($0)章 标题 \($0 * 7)</p>" }.joined()), false),
        ("OEBPS/Text/note.xhtml", xhtml("<p>脚注不应进入正文脚注不应进入正文脚注不应进入正文</p>"), false),
    ])

    return TypingTestSmokeFixture(epub: epub, container: container, zip: zip, le: le)
}

/// Extra arguments are real .epub files: each is parsed read-only and summarised.
func runTypingTestLibrarySmokeTest(probing files: [URL] = []) -> Bool {
    func fail(_ message: String) -> Bool { print("typing-test-library-smoke: FAIL \(message)"); return false }

    let fixture = typingTestSmokeFixture()
    let epub = fixture.epub, container = fixture.container, zip = fixture.zip, le = fixture.le

    do {
        let book = try TypingTestEPUBImporter.parse(data: epub, importedAt: 1)
        guard book.title == "测试之书", book.author == "某作者" else { return fail("metadata \(book.title)/\(book.author)") }
        guard book.chapters.count == 2 else { return fail("cover and non-linear note must be skipped, got \(book.chapters.map(\.title))") }
        guard book.chapters[0].title == "01 · 第一章清晨", book.chapters[1].title == "02 · Chapter Two" else { return fail("chapter titles \(book.chapters.map(\.title))") }
        let all = book.chapters.flatMap(\.passages)
        guard all.allSatisfy({ $0.count <= TypingTestPassageSplitter.pieceLimit && $0.count > 0 && $0 == $0.trimmingCharacters(in: .whitespacesAndNewlines) }) else { return fail("passage size/trim") }
        let parts0 = book.chapters[0].passages.count, parts1 = book.chapters[1].passages.count
        guard (2...3).contains(parts0), (3...5).contains(parts1) else {
            return fail("pieces should be short: chapter parts \(book.chapters.map(\.passages.count))")
        }
        // A tiny last piece may be merged into its neighbour (up to a sixth over the limit).
        guard book.chapters[0].passages.allSatisfy({ TypingTestPassageSplitter.typedLength($0) <= 940 }), book.chapters[1].passages.allSatisfy({ TypingTestPassageSplitter.typedLength($0) <= 1_170 }) else {
            return fail("piece length limits")
        }
        // Pieces end on a paragraph or sentence boundary, never mid-sentence.
        guard book.chapters[0].passages.dropLast().allSatisfy({ $0.hasSuffix("！") }),
              book.chapters[1].passages.allSatisfy({ $0.hasSuffix(".") }) else {
            return fail("pieces must end at paragraph/sentence ends: \(book.chapters[1].passages.map { String($0.suffix(6)) })")
        }
        // Chinese paragraphs stay marked with line breaks (the practice view indents them);
        // they are layout, not text, so the checks below read the text without them.
        guard book.chapters[0].passages.contains(where: { $0.contains("\n") }),
              book.chapters[0].passages.allSatisfy({ !$0.hasPrefix("\n") && !$0.hasSuffix("\n") && !$0.contains("\n\n") }) else {
            return fail("Chinese pieces must keep one line break between paragraphs: \(book.chapters[0].passages)")
        }
        guard book.chapters[1].passages.allSatisfy({ !$0.contains("\n") }) else { return fail("English paragraphs are joined by a space") }
        let first = book.chapters[0].passages.joined().replacingOccurrences(of: "\n", with: "")
        guard first.hasPrefix("第1段：清晨的街道很安静，老板把热气腾腾的豆浆放在柜台上。"), !first.contains("SCRIPTTEXT"),
              !first.contains("hidden comment"), !first.contains("han"), !first.contains("ignored head"), !first.contains("\n"),
              first.contains("汉字&一—结束了这一章的全部内容。") else { return fail("markup clean-up: prefix=\(first.hasPrefix("第1段：清晨的街道很安静，老板把热气腾腾的豆浆放在柜台上。")) script=\(first.contains("SCRIPTTEXT")) comment=\(first.contains("hidden comment")) han=\(first.contains("han")) head=\(first.contains("ignored head")) nl=\(first.contains("\n")) tail=\(Array(first.suffix(9).unicodeScalars.map { String($0.value, radix: 16) }))") }
        guard !first.contains("★"), !first.contains("※"), !first.contains("[12]"), !first.contains("http"), !first.contains("@"),
              !first.contains("，，"), !first.contains("。。"), !first.contains("----"), first.contains("请参阅附录") else {
            return fail("practice text must drop symbols, footnotes, links and repeated punctuation")
        }
        guard book.cut == TypingTestBook.currentCut else { return fail("a fresh import carries the current cut") }
        guard book.chapters[0].passages.allSatisfy({ TypingTestPassageSplitter.language(of: $0) == .chinese }),
              book.chapters[1].passages.allSatisfy({ TypingTestPassageSplitter.language(of: $0) == .english }) else { return fail("language detection") }
        guard book.chapters[1].passages[0].contains("Dr. Smith waved"), book.chapters[1].passages[0].contains("smiled & nodded.") else { return fail("english text") }
        let article = book.article(number: parts0 + 1)!
        guard article.id == "epub.\(book.id).\(parts0 + 1)", book.passageCount == parts0 + parts1,
              book.article(number: book.passageCount + 1) == nil,
              article.title.hasSuffix("02 · Chapter Two · 1/\(parts1)"), book.article(number: 1)?.title.hasSuffix("01 · 第一章清晨 · 1/\(parts0)") == true,
              article.version == TypingTestBook.currentCut,
              let scored = Optional(TypingTestSession(article: article, context: TypingTestContext(schemaID: "smoke"))) else { return fail("article ids") }
        scored.start(at: 1)
        guard scored.reconcileCommittedText(article.text, at: 11), scored.snapshot(at: 11).canComplete else { return fail("imported passage must be scorable") }
        guard try TypingTestEPUBImporter.parse(data: epub, importedAt: 9).id == book.id else { return fail("identity must follow the file bytes") }

        // Library round trip in an isolated root.
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("rimes-library-\(UUID().uuidString)", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: root) }
        let file = root.appendingPathComponent("book.epub")
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        try epub.write(to: file)
        let library = TypingTestLibraryStore(storageRoot: root)
        guard library.books.isEmpty, case .added = try library.importEPUB(at: file) else { return fail("first import") }
        guard case .alreadyPresent = try library.importEPUB(at: file), library.books.count == 1 else { return fail("duplicate import must not add") }
        let reopened = TypingTestLibraryStore(storageRoot: root)
        guard reopened.books.count == 1, reopened.article(id: "epub.\(book.id).\(parts0 + 1)")?.text == article.text,
              reopened.article(id: "epub.\(book.id).9999") == nil, reopened.article(id: "rimes.morning-street") == nil else { return fail("reload/resolve") }
        guard reopened.remove(id: book.id), reopened.books.isEmpty,
              TypingTestLibraryStore(storageRoot: root).books.isEmpty else { return fail("remove") }

        // An older cut of the same file is replaced on re-import; a current one is left alone.
        let oldRoot = root.appendingPathComponent("old-cut", isDirectory: true)
        let oldLibrary = TypingTestLibraryStore(storageRoot: oldRoot)
        var older = book
        older.cut = 1
        guard case .added = try oldLibrary.add(older), oldLibrary.book(id: book.id)?.cut == 1 else { return fail("seed an old-cut book") }
        guard case .replaced = try oldLibrary.importEPUB(at: file), oldLibrary.book(id: book.id)?.cut == TypingTestBook.currentCut,
              oldLibrary.books.count == 1, TypingTestLibraryStore(storageRoot: oldRoot).book(id: book.id)?.cut == TypingTestBook.currentCut else {
            return fail("re-import must replace an older cut")
        }
        guard case .alreadyPresent = try oldLibrary.importEPUB(at: file) else { return fail("re-import of a current cut must be a no-op") }

        // Hostile or unsupported input fails closed with an error, never a crash.
        for (name, data) in [("not a zip", Data("hello".utf8)), ("empty", Data()),
                             ("truncated", epub.prefix(epub.count / 2)),
                             ("no package", zip([("a.txt", Data("x".utf8), false)]))] {
            do { _ = try TypingTestEPUBImporter.parse(data: Data(data)); return fail("\(name) must be rejected") } catch {}
        }
        var corrupt = [UInt8](epub)
        if let spot = corrupt.indices.dropFirst(300).first(where: { corrupt[$0] != 0 }) { for i in spot..<min(spot + 40, corrupt.count - 64) { corrupt[i] ^= 0xFF } }
        _ = try? TypingTestEPUBImporter.parse(data: Data(corrupt)) // any outcome but a trap is acceptable
        let bomb = zip([("META-INF/container.xml", Data(container.utf8), true)])
        var tampered = [UInt8](bomb)
        // Declare a 1 GiB uncompressed size in the central directory entry.
        if let directory = (0..<tampered.count - 4).first(where: { Array(tampered[$0..<$0 + 4]) == le(0x0201_4B50, 4) }) {
            tampered.replaceSubrange((directory + 24)..<(directory + 28), with: le(1 << 30, 4))
        }
        do { _ = try TypingTestEPUBImporter.parse(data: Data(tampered)); return fail("oversized declared entry must be rejected") } catch {}

        // Practice text: dot leaders, loose points and never-typed spaces go; prose punctuation stays.
        let cleaning: [(String, String)] = [
            ("第 5 章为什么有人屡屡戒不了烟 ..............................45下一节", "第5章为什么有人屡屡戒不了烟下一节"),
            ("他说： “好的 ”，然后走了。", "他说：“好的”，然后走了。"),
            ("丧失了乐趣。 2、不要等着。 3、好。", "丧失了乐趣。2、不要等着。3、好。"),
            ("当你分发 STRAND 时，你会 ( 被骗 ) 。", "当你分发STRAND时，你会(被骗)。"),
            ("等一等...... 然后继续", "等一等...... 然后继续"),
            ("苹果 . 香蕉 · 橘子", "苹果香蕉橘子"),
            ("“Hi,” she said. “Bye.” ", "“Hi,” she said. “Bye.”"),
        ]
        for (raw, expected) in cleaning where TypingTestPassageSplitter.practiceText(raw) != expected {
            return fail("practice text \(raw.debugDescription) became \(TypingTestPassageSplitter.practiceText(raw).debugDescription), expected \(expected.debugDescription)")
        }
        guard TypingTestPassageSplitter.leaderCount("甲 .......... 3 乙 .......... 9 丙 .......... 12") == 3 else { return fail("catalogue leaders must be counted") }

        // Splitter limits for one huge unbroken sentence.
        let huge = TypingTestPassageSplitter.passages(from: [String(repeating: "字", count: 5_000)])
        guard huge.count == 7, huge.allSatisfy({ $0.count <= 800 }) else { return fail("unbroken text must be hard-cut under the limit") }
        for url in files {
            let started = Date()
            do {
                let real = try TypingTestLibraryStore.parseFile(at: url)
                let longest = real.chapters.flatMap(\.passages).map(TypingTestPassageSplitter.typedLength).max() ?? 0
                print("probe \(url.lastPathComponent): \"\(real.title)\" / \(real.author) — \(real.chapters.count) chapters, \(real.passageCount) passages, \(real.characterCount) chars, longest \(longest), \(String(format: "%.2f", Date().timeIntervalSince(started)))s")
                guard TypingTestLibraryStore.valid(real) else { return fail("probe \(url.lastPathComponent) produced an invalid book") }
            } catch { print("probe \(url.lastPathComponent): \(error.localizedDescription)") }
        }
        print("typing-test-library-smoke: PASS stored/deflated zip, markup clean-up, passages ≤ limit, id stability, library round trip, hostile input")
        return true
    } catch { return fail("\(error)") }
}
