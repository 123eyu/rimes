import XCTest
import ZIPFoundation
@testable import RIMES

final class RimeSchemeImportTests: XCTestCase {
    func testCodedTextDictionaryStagesWithOriginalIdentity() throws {
        let data = Data("\u{FEFF}# public fixture\n你好\tnihao\t100\n世界\tshijie\n".utf8)
        let review = try RimeSchemeImportService.inspectTextDictionary(data: data, sourceName: "个人词库")
        XCTAssertEqual(review.schemes.count, 1)
        XCTAssertEqual(review.schemes[0].blockingIssues, [])
        let destination = temporaryDirectory(); defer { try? FileManager.default.removeItem(at: destination) }
        let staged = try RimeSchemeImportService.stage(review: review, selectedSchemaIDs: review.schemes.map(\.id), destinationRoot: destination)
        XCTAssertEqual(staged.sourceDigest, review.archiveSHA256)
        XCTAssertTrue(try String(contentsOf: staged.rootURL.appendingPathComponent(staged.schemaIDs[0] + ".dict.yaml")).contains("你好\tnihao\t100"))
        for invalid in ["只有文字", "字\t拼音", "字\tabc\t-1", "字\tABC"] {
            XCTAssertThrowsError(try RimeSchemeImportService.inspectTextDictionary(data: Data(invalid.utf8), sourceName: "bad"))
        }
    }

    func testEscapedLuaModuleNamesAreLeftToNativeValidation() throws {
        var files = fixture
        files["lua/demo/topup.lua"] = Data(#"local x = require('demo.\116opup'); return {}"#.utf8)
        XCTAssertNoThrow(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "Lua escaped module"))
    }
    func testInspectsSchemeIdentitySeparatelyFromFrontendLayout() throws {
        var files = fixture
        files["hamster.yaml"] = Data("keyboards: [{name: xingmao, rows: []}]".utf8)
        let review = try RimeSchemeImportService.inspect(data: archive(files), sourceName: "示例输入方案")
        XCTAssertEqual(review.schemes.map(\.id), ["demo"])
        XCTAssertEqual(review.schemes.first?.name, "测试音形")
        XCTAssertEqual(review.schemes.first?.blockingIssues, [])
        XCTAssertTrue(review.schemes.first?.recommended == true)
        XCTAssertEqual(review.archiveSHA256.count, 64)
        XCTAssertFalse(review.schemes.contains { $0.id == "xingmao" })
    }

    func testStagePreservesResourcesAndSchemaPatchButIsolatesGlobalSettings() throws {
        var files = fixture
        files["default.custom.yaml"] = Data("patch: {schema_list: [{schema: replace_existing}]}".utf8)
        files["installation.yaml"] = Data("installation_id: private".utf8)
        files["demo.custom.yaml"] = Data("patch: {speller/auto_select: true}".utf8)
        files["opencc/test.json"] = Data(#"{"conversion_chain":[{"dict":{"file":"test.txt"}}]}"#.utf8)
        files["opencc/test.txt"] = Data("甲\t乙\n".utf8)
        let review = try RimeSchemeImportService.inspect(data: archive(files), sourceName: "示例")
        let destination = temporaryDirectory(); defer { try? FileManager.default.removeItem(at: destination) }
        let staged = try RimeSchemeImportService.stage(review: review, selectedSchemaIDs: ["demo"], destinationRoot: destination)
        XCTAssertNotNil(UUID(uuidString: staged.id))
        XCTAssertEqual(staged.schemaIDs, ["demo"])
        XCTAssertEqual(try Data(contentsOf: staged.rootURL.appendingPathComponent("lua/demo/topup.lua")), files["lua/demo/topup.lua"])
        XCTAssertEqual(try Data(contentsOf: staged.rootURL.appendingPathComponent("demo.custom.yaml")), files["demo.custom.yaml"])
        XCTAssertEqual(try Data(contentsOf: staged.rootURL.appendingPathComponent("opencc/test.txt")), files["opencc/test.txt"])
        XCTAssertFalse(FileManager.default.fileExists(atPath: staged.rootURL.appendingPathComponent("default.custom.yaml").path))
        XCTAssertFalse(FileManager.default.fileExists(atPath: staged.rootURL.appendingPathComponent("installation.yaml").path))
        let defaults = try String(contentsOf: staged.rootURL.appendingPathComponent("default.yaml"), encoding: .utf8)
        XCTAssertTrue(defaults.contains("schema: demo"))
        XCTAssertTrue(defaults.contains("key_binder:"))
        XCTAssertFalse(defaults.contains("switcher:"))
        XCTAssertFalse(FileManager.default.fileExists(atPath: staged.rootURL.appendingPathComponent("build").path), "Staging is not deployment.")
        let again = try RimeSchemeImportService.stage(review: review, selectedSchemaIDs: ["demo"], destinationRoot: destination)
        XCTAssertNotEqual(staged.id, again.id)
    }

    func testArchiveWithEnclosingFolderStagesAtPackageRoot() throws {
        let wrapped = Dictionary(uniqueKeysWithValues: fixture.map { ("方案/" + $0.key, $0.value) })
        let review = try RimeSchemeImportService.inspect(data: archive(wrapped), sourceName: "包")
        let destination = temporaryDirectory(); defer { try? FileManager.default.removeItem(at: destination) }
        let staged = try RimeSchemeImportService.stage(review: review, selectedSchemaIDs: ["demo"], destinationRoot: destination)
        XCTAssertTrue(FileManager.default.fileExists(atPath: staged.rootURL.appendingPathComponent("demo.schema.yaml").path))
        XCTAssertFalse(FileManager.default.fileExists(atPath: staged.rootURL.appendingPathComponent("方案").path))
    }

    func testMissingPrimaryDictionaryAndSchemaDependencyBlockStage() throws {
        var files = fixture
        files.removeValue(forKey: "demo.dict.yaml")
        files["demo.schema.yaml"] = Data(schema.replacingOccurrences(of: "name: 测试音形", with: "name: 测试音形\n  dependencies: [missing_schema]").utf8)
        let review = try RimeSchemeImportService.inspect(data: archive(files), sourceName: "不完整")
        let issues = try XCTUnwrap(review.schemes.first).blockingIssues.joined()
        XCTAssertTrue(issues.contains("demo.dict.yaml"))
        XCTAssertTrue(issues.contains("missing_schema.schema.yaml"))
        XCTAssertThrowsError(try RimeSchemeImportService.stage(review: review, selectedSchemaIDs: ["demo"], destinationRoot: temporaryDirectory()))
    }

    func testMissingDictionaryImportAndCyclicDictionaryAreDiagnosed() throws {
        var files = fixture
        files["demo.dict.yaml"] = Data("---\nname: demo\nimport_tables: [absent]\n...\n".utf8)
        let review = try RimeSchemeImportService.inspect(data: archive(files), sourceName: "缺少词库")
        XCTAssertTrue(review.schemes[0].blockingIssues.contains { $0.contains("absent.dict.yaml") })
        files["absent.dict.yaml"] = Data("---\nname: absent\nimport_tables: [demo]\n...\n".utf8)
        XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "词典循环"))
    }

    func testRequiredLuaDependencyMissingFailsButOptionalModuleAndDesktopToolAreReported() throws {
        var files = fixture
        files["lua/demo/topup.lua"] = Data("local m = require('demo.missing')\nreturn m".utf8)
        XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "缺少 Lua"))
        files["lua/demo/topup.lua"] = Data("local ok, lfs = pcall(require, 'lfs')\nlocal launch = os.execute\nreturn function() return 2 end".utf8)
        let review = try RimeSchemeImportService.inspect(data: archive(files), sourceName: "含可选功能")
        XCTAssertTrue(review.warnings.contains { $0.contains("lfs") })
        XCTAssertTrue(review.unsupportedFeatures.contains { $0.contains("桌面") })
        XCTAssertEqual(review.schemes[0].blockingIssues, [])
    }

    func testRejectsOpenCCExternalPathsEvenInsideNestedGroups() throws {
        for path in ["/private/file", "../outside.txt", "nested/../../outside.txt", "C:\\outside.txt"] {
            var files = fixture
            files["opencc/bad.json"] = try JSONSerialization.data(withJSONObject: ["conversion_chain": [["dict": ["type": "group", "dicts": [["type": "text", "file": path]]]]]])
            XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "外部路径"), path)
        }
    }

    func testRejectsSchemaNativeResourcePathsOutsidePackage() throws {
        for (key, value) in [("dictionary", "../outside"), ("prism", "/private/cache"),
                             ("user_dict", "../../user"), ("opencc_config", "../other.json")] {
            var files = fixture
            files["demo.schema.yaml"] = Data((schema + "\nauxiliary:\n  \(key): '\(value)'\n").utf8)
            XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "不安全原生路径"), key)
        }
        var files = fixture
        files["symbols.yaml"] = Data("punctuator: {__include: '../../outside:/punctuator'}".utf8)
        XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "不安全预设路径"))
    }

    func testRimeIceSubdirectoryDictionariesAndReadOnlyMixedPhrasesArePreserved() throws {
        var files = fixture
        files["demo.schema.yaml"] = Data((schema + "\ncn_en:\n  dictionary: ''\n  user_dict: en_dicts/cn_en\n  db_class: stabledb\n").utf8)
        files["demo.dict.yaml"] = Data("---\nname: demo\nimport_tables: [cn_dicts/base]\n...\n".utf8)
        files["cn_dicts/base.dict.yaml"] = Data("---\nname: base\n...\n雾凇\twu song\n".utf8)
        files["en_dicts/cn_en.txt"] = Data("哆啦A梦\tdo la a meng\n".utf8)
        let review = try RimeSchemeImportService.inspect(data: archive(files), sourceName: "雾凇目录结构")
        XCTAssertEqual(review.schemes[0].blockingIssues, [])
        let destination = temporaryDirectory(); defer { try? FileManager.default.removeItem(at: destination) }
        let staged = try RimeSchemeImportService.stage(review: review, selectedSchemaIDs: ["demo"], destinationRoot: destination)
        for path in ["cn_dicts/base.dict.yaml", "en_dicts/cn_en.txt"] {
            XCTAssertEqual(try Data(contentsOf: staged.rootURL.appendingPathComponent(path)), files[path], path)
        }
        files.removeValue(forKey: "cn_dicts/base.dict.yaml")
        let incomplete = try RimeSchemeImportService.inspect(data: archive(files), sourceName: "缺失子词典")
        XCTAssertTrue(incomplete.schemes[0].blockingIssues.contains { $0.contains("cn_dicts/base.dict.yaml") })
    }

    func testSubdirectoryUserDictionaryRequiresPackagedReadOnlyTableIncludingEffectivePatch() throws {
        let auxiliary = "\ncn_en:\n  dictionary: ''\n  user_dict: en_dicts/cn_en\n  db_class: stabledb\n"
        var files = fixture
        files["demo.schema.yaml"] = Data((schema + auxiliary).utf8)
        XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "未随包提供"))
        files["en_dicts/cn_en.txt"] = Data("混合\thun he\n".utf8)
        for database in ["tabledb", "userdb", "plain_userdb"] {
            files["demo.schema.yaml"] = Data((schema + auxiliary.replacingOccurrences(of: "stabledb", with: database)).utf8)
            XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "写入型路径"), database)
        }
        files["demo.schema.yaml"] = Data((schema + auxiliary).utf8)
        files["demo.custom.yaml"] = Data("patch: {cn_en/db_class: tabledb}\n".utf8)
        XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "补丁改变数据库类型"))
        files["demo.custom.yaml"] = Data("patch: {'/cn_en/db_class': tabledb}\n".utf8)
        XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "绝对配置键路径改变数据库类型"))
        files["demo.custom.yaml"] = Data("patch: {cn_en: {dictionary: '', user_dict: en_dicts/cn_en}}\n".utf8)
        XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "整个命名空间补丁丢失只读类型"))
        files["demo.custom.yaml"] = Data("patch: {cn_en/db_class/=: tabledb}\n".utf8)
        XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "显式替换数据库类型"))
        files["demo.custom.yaml"] = Data("patch: {cn_en/db_class/+: malicious}\n".utf8)
        XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "追加数据库类型"))
        files["demo.custom.yaml"] = Data("patch: {cn_en/user_dict/+: /../../outside}\n".utf8)
        XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "补丁追加路径逃逸"))
        files.removeValue(forKey: "demo.custom.yaml")
        files["demo.schema.yaml"] = Data((schema + auxiliary.replacingOccurrences(of: "stabledb", with: "tabledb") + "'cn_en/db_class': stabledb\n").utf8)
        XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "普通 YAML 平铺键不能覆盖真实类型"))
        files["demo.schema.yaml"] = Data((schema + auxiliary.replacingOccurrences(of: "stabledb", with: "tabledb")).utf8)
        files["demo.custom.yaml"] = Data("patch: {cn_en/+: {'/db_class': stabledb}}\n".utf8)
        XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "合并树中的字面路径键不能覆盖真实类型"))
        files["demo.schema.yaml"] = Data((schema + auxiliary.replacingOccurrences(of: "user_dict: en_dicts/cn_en", with: "user_dict: custom_phrase")).utf8)
        files["demo.custom.yaml"] = Data("patch: {cn_en/user_dict: en_dicts/cn_en}\n".utf8)
        XCTAssertNoThrow(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "补丁继承只读类型"))
    }

    func testDictionaryAndReadOnlyUserDictionaryPathsStillRejectTraversalAndAbsoluteNames() throws {
        for path in ["/private/outside", "../outside", "cn_dicts/../../outside", "cn_dicts/./base", "cn_dicts//base", "cn_dicts/base/", "C:/outside", "C:\\outside"] {
            var files = fixture
            files["demo.dict.yaml"] = Data("---\nname: demo\nimport_tables: ['\(path)']\n...\n".utf8)
            XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "不安全子词典"), path)
            files = fixture
            files["demo.schema.yaml"] = Data((schema + "\ncn_en:\n  user_dict: '\(path)'\n  db_class: stabledb\n").utf8)
            XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "不安全只读词典"), path)
        }
    }

    func testEmptyUserDictionaryKeepsNativeRootLocalNameSemantics() throws {
        var files = fixture
        files["demo.schema.yaml"] = Data((schema + "\nauxiliary: {dictionary: '', user_dict: '', db_class: userdb}\n").utf8)
        XCTAssertNoThrow(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "空用户词典名称"))
    }

    func testRejectsTraversalLinkEncryptionAndCompressionBomb() throws {
        var files = fixture; files["../escape.txt"] = Data("escape".utf8)
        XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "路径逃逸"))
        let linked = try archive(fixture, symlink: "lua/external.lua")
        XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: linked, sourceName: "符号链接"))
        var encrypted = try archive(fixture)
        let central = try XCTUnwrap(encrypted.range(of: Data([0x50, 0x4b, 0x01, 0x02]))).lowerBound
        encrypted[central + 8] |= 1
        XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: encrypted, sourceName: "加密"))
        files = fixture; files["bomb.txt"] = Data(repeating: 32, count: 150_000)
        XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files, compressed: true), sourceName: "压缩炸弹"))
    }

    func testCorruptDictionaryBodyFailsStagingAndRemovesPartialDirectory() throws {
        var files = fixture
        // Ensure the header reader stops before reading the changed body, while staging must
        // consume every byte and verify CRC before publishing anything.
        files["demo.dict.yaml"] = Data(("---\nname: demo\nversion: '1'\n...\n" + String(repeating: "示例\taa\n", count: 2_000)).utf8)
        var broken = try archive(files)
        let body = try XCTUnwrap(broken.range(of: Data("示例\taa".utf8))).lowerBound
        broken[body] = 0x41
        let review = try RimeSchemeImportService.inspect(data: broken, sourceName: "正文损坏")
        let destination = temporaryDirectory(); defer { try? FileManager.default.removeItem(at: destination) }
        XCTAssertThrowsError(try RimeSchemeImportService.stage(review: review, selectedSchemaIDs: ["demo"], destinationRoot: destination))
        XCTAssertEqual(try FileManager.default.contentsOfDirectory(atPath: destination.path), [])
    }

    func testIllegalYAMLControlsAreExplicitlyRepairedOnlyInStagedConfiguration() throws {
        var files = fixture
        let original = Data("punctuator:\n  symbols:\n    '/x': ['a\u{11}\u{10}b']\n".utf8)
        files["symbols.yaml"] = original
        let review = try RimeSchemeImportService.inspect(data: archive(files), sourceName: "含非法字符")
        XCTAssertTrue(review.warnings.contains { $0.contains("symbols.yaml") && $0.contains("2 个") && $0.contains("U+0010") && $0.contains("U+0011") })
        let destination = temporaryDirectory(); defer { try? FileManager.default.removeItem(at: destination) }
        let staged = try RimeSchemeImportService.stage(review: review, selectedSchemaIDs: ["demo"], destinationRoot: destination)
        let repaired = try Data(contentsOf: staged.rootURL.appendingPathComponent("symbols.yaml"))
        XCTAssertNotEqual(repaired, original)
        XCTAssertTrue(String(decoding: repaired, as: UTF8.self).contains("['ab']"))
        XCTAssertEqual(files["symbols.yaml"], original)
    }

    func testYAMLDepthBudgetUsesParserEventsDespiteQuotedClosingBrackets() throws {
        var files = fixture
        let deep = "value: " + String(repeating: "[']',", count: 90) + "null" + String(repeating: "]", count: 90)
        files["bad.yaml"] = Data(deep.utf8)
        XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "深层 YAML"))
        files["bad.yaml"] = Data("a: &a [x, x]\nb: &b [*a, *a]\nc: &c [*b, *b]\n".utf8)
        XCTAssertNoThrow(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "普通别名"))
    }

    func testDictionaryHeaderAllowsTabAfterTerminatorAndBlankTabLineWithoutChangingBody() throws {
        var files = fixture
        let original = Data("# exported\t\n\t\n---\t\nname: demo\t\nversion: '1'\t\n...\t\n示例\taa\n".utf8)
        files["demo.dict.yaml"] = original
        let review = try RimeSchemeImportService.inspect(data: archive(files), sourceName: "表格词典")
        let destination = temporaryDirectory(); defer { try? FileManager.default.removeItem(at: destination) }
        let staged = try RimeSchemeImportService.stage(review: review, selectedSchemaIDs: ["demo"], destinationRoot: destination)
        XCTAssertEqual(try Data(contentsOf: staged.rootURL.appendingPathComponent("demo.dict.yaml")), original)
    }

    func testSymbolDuplicatesKeepLastRimeValueWithWarningButSchemaDuplicatesFail() throws {
        var files = fixture
        files["symbols.yaml"] = Data("punctuator:\n  symbols:\n    '/x': [first]\n    '/x': [last]\n".utf8)
        let review = try RimeSchemeImportService.inspect(data: archive(files), sourceName: "重复符号")
        XCTAssertTrue(review.warnings.contains { $0.contains("重复键") && $0.contains("/x") && $0.contains("最后") })
        let destination = temporaryDirectory(); defer { try? FileManager.default.removeItem(at: destination) }
        let staged = try RimeSchemeImportService.stage(review: review, selectedSchemaIDs: ["demo"], destinationRoot: destination)
        let symbols = try String(contentsOf: staged.rootURL.appendingPathComponent("symbols.yaml"), encoding: .utf8)
        XCTAssertTrue(symbols.contains("last")); XCTAssertFalse(symbols.contains("first"))
        files["demo.schema.yaml"] = Data((schema + "\nschema: {schema_id: replacement}\n").utf8)
        XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "重复方案键"))
    }

    func testOpenCCSameDirectoryDotReferenceIsAllowed() throws {
        var files = fixture
        files["opencc/safe.json"] = Data(#"{"conversion_chain":[{"dict":{"file":"./safe.txt"}}]}"#.utf8)
        files["opencc/safe.txt"] = Data("甲\t乙\n".utf8)
        let review = try RimeSchemeImportService.inspect(data: archive(files), sourceName: "合法 OpenCC")
        XCTAssertFalse(review.warnings.contains { $0.contains("未随包提供的 ./safe.txt") })
    }

    func testDeploymentDependencyOrderDoesNotExposeAuxiliarySchemasAsSelected() throws {
        var files = fixture
        files["demo.schema.yaml"] = Data(schema.replacingOccurrences(of: "name: 测试音形", with: "name: 测试音形\n  dependencies: [helper]").utf8)
        files["helper.schema.yaml"] = Data("schema: {schema_id: helper, name: 辅助}\ntranslator: {dictionary: demo}\n".utf8)
        let review = try RimeSchemeImportService.inspect(data: archive(files), sourceName: "方案依赖")
        let destination = temporaryDirectory(); defer { try? FileManager.default.removeItem(at: destination) }
        let staged = try RimeSchemeImportService.stage(review: review, selectedSchemaIDs: ["demo"], destinationRoot: destination)
        XCTAssertEqual(staged.schemaIDs, ["demo"])
        XCTAssertEqual(staged.deploymentSchemaIDs, ["helper", "demo"])
        files["helper.schema.yaml"] = Data("schema: {schema_id: helper, name: 辅助, dependencies: [demo]}\ntranslator: {dictionary: demo}\n".utf8)
        let cyclic = try RimeSchemeImportService.inspect(data: archive(files), sourceName: "依赖循环")
        let reciprocal = try RimeSchemeImportService.stage(review: cyclic, selectedSchemaIDs: ["demo"], destinationRoot: destination)
        XCTAssertEqual(reciprocal.deploymentSchemaIDs, ["helper", "demo"])
        XCTAssertEqual(reciprocal.schemaIDs, ["demo"])
    }

    func testUpstreamArchiveIgnoresFinderMetadataAndCompiledSchemasDuringRootDiscovery() throws {
        var files = Dictionary(uniqueKeysWithValues: fixture.map { ("release/source/" + $0.key, $0.value) })
        files["__MACOSX/release/source/._demo.schema.yaml"] = Data([0, 1, 2])
        files["release/source/build/demo.schema.yaml"] = Data("compiled: true".utf8)
        files["release/source/examples/other.schema.yaml"] = Data("example: true".utf8)
        let review = try RimeSchemeImportService.inspect(data: archive(files), sourceName: "上游原包")
        XCTAssertEqual(review.schemes.map(\.id), ["demo"])
        let destination = temporaryDirectory(); defer { try? FileManager.default.removeItem(at: destination) }
        let staged = try RimeSchemeImportService.stage(review: review, selectedSchemaIDs: ["demo"], destinationRoot: destination)
        XCTAssertTrue(FileManager.default.fileExists(atPath: staged.rootURL.appendingPathComponent("demo.schema.yaml").path))
        XCTAssertFalse(FileManager.default.fileExists(atPath: staged.rootURL.appendingPathComponent("build").path))
        files["independent/other.schema.yaml"] = Data("schema: {schema_id: other}".utf8)
        XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "多套独立根目录"))
    }

    func testRimeReferenceSpellingsRemainUnmodifiedAndOptionalReferencesDoNotBlock() throws {
        for reference in ["/punctuator", ":/punctuator", "symbols.yaml:/punctuator", "absent:/punctuator?"] {
            var files = fixture
            files["symbols.yaml"] = Data("punctuator: {symbols: {}}".utf8)
            let original = Data((schema + "\nextra: {__include: '\(reference)'}\n").utf8)
            files["demo.schema.yaml"] = original
            let review = try RimeSchemeImportService.inspect(data: archive(files), sourceName: reference)
            let destination = temporaryDirectory(); defer { try? FileManager.default.removeItem(at: destination) }
            let staged = try RimeSchemeImportService.stage(review: review, selectedSchemaIDs: ["demo"], destinationRoot: destination)
            XCTAssertEqual(try Data(contentsOf: staged.rootURL.appendingPathComponent("demo.schema.yaml")), original)
        }
        for reference in ["../outside:/x?", "/outside:/x?", "C:\\outside:/x?", "symbols:/../../x"] {
            var files = fixture
            files["demo.schema.yaml"] = Data((schema + "\nextra: {__include: '\(reference)'}\n").utf8)
            XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: reference))
        }
    }

    func testCustomPatchReplacesMissingBaseDictionaryAndDependenciesBeforeChecking() throws {
        var files = fixture
        files["demo.schema.yaml"] = Data(schema.replacingOccurrences(of: "dictionary: demo", with: "dictionary: unshipped")
            .replacingOccurrences(of: "name: 测试音形", with: "name: 测试音形\n  dependencies: [unshipped]").utf8)
        files["demo.custom.yaml"] = Data("patch: {translator/dictionary: demo, schema/dependencies: [], schema/name: 补丁名称}\n".utf8)
        let review = try RimeSchemeImportService.inspect(data: archive(files), sourceName: "覆盖配置")
        XCTAssertEqual(review.schemes[0].name, "补丁名称")
        XCTAssertEqual(review.schemes[0].dependencies, [])
        XCTAssertEqual(review.schemes[0].blockingIssues, [])
        files["demo.custom.yaml"] = Data("patch: {translator: {dictionary: missing}}\n".utf8)
        let bad = try RimeSchemeImportService.inspect(data: archive(files), sourceName: "真实缺失")
        XCTAssertTrue(bad.schemes[0].blockingIssues.contains { $0.contains("missing.dict.yaml") })
    }

    func testLuaLexerSkipsQuotedExamplesAndLongCommentsButFindsRealImports() throws {
        var files = fixture
        files["lua/demo/topup.lua"] = Data(#"""
        --[==[ require('unused.block') ]==]
        local example = "require('unused.string')"
        local long = [=[require('unused.long')]=]
        -- require('unused.line')
        local optional = pcall(require, 'optional.lib')
        local core = require [[demo.core]]
        return function() return 2 end
        """#.utf8)
        files["lua/demo/core/init.lua"] = Data("return {}".utf8)
        let review = try RimeSchemeImportService.inspect(data: archive(files), sourceName: "标准 Lua")
        XCTAssertTrue(review.warnings.contains { $0.contains("optional.lib") })
        XCTAssertFalse(review.warnings.contains { $0.contains("unused") })
        files["lua/demo/topup.lua"] = Data("pcall(print, 'safe'); local m = require('required.lib')".utf8)
        XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "同行必需依赖"))
    }

    func testLuaComponentMemberSyntaxAndInitModulesArePreserved() throws {
        var files = fixture
        let source = schema.replacingOccurrences(of: "*demo/topup", with: "*demo.module*processor")
        files["demo.schema.yaml"] = Data(source.utf8)
        files["lua/demo/module/init.lua"] = Data("return {processor = function() return 2 end}".utf8)
        let review = try RimeSchemeImportService.inspect(data: archive(files), sourceName: "Lua 子组件")
        XCTAssertEqual(review.schemes[0].blockingIssues, [])
        files.removeValue(forKey: "lua/demo/module/init.lua")
        let missing = try RimeSchemeImportService.inspect(data: archive(files), sourceName: "缺失子组件")
        XCTAssertTrue(missing.schemes[0].blockingIssues.contains { $0.contains("demo/module.lua") })
    }

    func testManifestVersionAndGlobalSelectionIntentWithoutOverwritingPreferences() throws {
        var files = fixture
        files["default.yaml"] = Data("schema_list: []\n".utf8)
        files["default.custom.yaml"] = Data("patch: {schema_list: [{schema: demo}], menu/page_size: 9}\n".utf8)
        var review = try RimeSchemeImportService.inspect(data: archive(files), sourceName: "用户方案包")
        XCTAssertTrue(review.schemes[0].recommended)
        files["rimes-package.yaml"] = Data("rimes_package: {format: 1, recommended_schemes: []}\n".utf8)
        review = try RimeSchemeImportService.inspect(data: archive(files), sourceName: "显式清单")
        XCTAssertFalse(review.schemes[0].recommended)
        files["rimes-package.yaml"] = Data("rimes_package: {format: 2}\n".utf8)
        XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "未来版本"))
    }

    func testLargeSourceDictionaryDoesNotRequireSplittingAt32MB() throws {
        var files = fixture
        var dictionary = Data("---\nname: demo\nversion: '1'\n...\n".utf8)
        dictionary.append(Data(repeating: 32, count: 33 * 1_024 * 1_024))
        files["demo.dict.yaml"] = dictionary
        let review = try RimeSchemeImportService.inspect(data: archive(files), sourceName: "大词典")
        XCTAssertEqual(review.schemes[0].blockingIssues, [])
        let destination = temporaryDirectory(); defer { try? FileManager.default.removeItem(at: destination) }
        let staged = try RimeSchemeImportService.stage(review: review, selectedSchemaIDs: ["demo"], destinationRoot: destination)
        let size = try staged.rootURL.appendingPathComponent("demo.dict.yaml").resourceValues(forKeys: [.fileSizeKey]).fileSize
        XCTAssertEqual(size, dictionary.count)
    }

    func testCompressedDictionaryHeaderCanStopBeforeTheBodyEnds() throws {
        var files = fixture
        let rows = (0..<8_000).map { "词条\($0)\taa\t\($0 + 1)" }.joined(separator: "\n")
        let dictionary = Data(("---\nname: demo\nversion: '1'\n...\n" + rows + "\n").utf8)
        files["demo.dict.yaml"] = dictionary
        let review = try RimeSchemeImportService.inspect(data: archive(files, compressed: true), sourceName: "压缩词典")
        XCTAssertEqual(review.schemes[0].blockingIssues, [])
        let destination = temporaryDirectory(); defer { try? FileManager.default.removeItem(at: destination) }
        let staged = try RimeSchemeImportService.stage(review: review, selectedSchemaIDs: ["demo"], destinationRoot: destination)
        XCTAssertEqual(try Data(contentsOf: staged.rootURL.appendingPathComponent("demo.dict.yaml")), dictionary)
    }

    func testLuaReverseLookupDataIsStagedButCompiledSchemaCachesAreNot() throws {
        var files = fixture
        let bytes = Data([0x52, 0x49, 0x4d, 0x45])
        files["lua/data/charset.reverse.bin"] = bytes
        files["build/demo.table.bin"] = bytes
        let review = try RimeSchemeImportService.inspect(data: archive(files), sourceName: "字符集数据")
        let destination = temporaryDirectory(); defer { try? FileManager.default.removeItem(at: destination) }
        let staged = try RimeSchemeImportService.stage(review: review, selectedSchemaIDs: ["demo"], destinationRoot: destination)
        XCTAssertEqual(try Data(contentsOf: staged.rootURL.appendingPathComponent("lua/data/charset.reverse.bin")), bytes)
        XCTAssertFalse(FileManager.default.fileExists(atPath: staged.rootURL.appendingPathComponent("build/demo.table.bin").path))
    }

    private var schema: String {
        """
        schema:
          schema_id: demo
          name: 测试音形
        engine:
          processors: [lua_processor@*demo/topup, speller, selector, express_editor]
          segmentors: [abc_segmentor]
          translators: [table_translator]
        translator:
          dictionary: demo
        speller:
          alphabet: abcdefghijklmnopqrstuvwxyz
        """
    }
    func testPortablePathsAndUnavailableModelsAreDiagnosed() throws {
        for path in ["CON.txt", "dir/aux.yaml", "word . ", "model.gram"] {
            var files = fixture
            files[path] = Data("fixture".utf8)
            XCTAssertThrowsError(try RimeSchemeImportService.inspect(data: archive(files), sourceName: "portable"), path)
        }
    }

    private var fixture: [String: Data] {
        ["demo.schema.yaml": Data(schema.utf8),
         "demo.dict.yaml": Data("---\nname: demo\nversion: '1'\n...\n示例\taa\n".utf8),
         "lua/demo/topup.lua": Data("return function(key, env) return 2 end".utf8),
         "default.yaml": Data("schema_list: [{schema: demo}]\nswitcher: {caption: old}\nkey_binder: {bindings: []}\n".utf8)]
    }
    private func temporaryDirectory() -> URL { FileManager.default.temporaryDirectory.appendingPathComponent("scheme-import-test-" + UUID().uuidString) }
    private func archive(_ files: [String: Data], symlink: String? = nil, compressed: Bool = false) throws -> Data {
        let archive = try Archive(data: Data(), accessMode: .create)
        for (path, data) in files.sorted(by: { $0.key < $1.key }) {
            try archive.addEntry(with: path, type: .file, uncompressedSize: Int64(data.count), compressionMethod: compressed ? .deflate : .none) { offset, length in
                data.subdata(in: Int(offset)..<Int(offset) + length)
            }
        }
        if let symlink {
            let data = Data("/outside".utf8)
            try archive.addEntry(with: symlink, type: .symlink, uncompressedSize: Int64(data.count)) { offset, length in data.subdata(in: Int(offset)..<Int(offset) + length) }
        }
        return try XCTUnwrap(archive.data)
    }
}
