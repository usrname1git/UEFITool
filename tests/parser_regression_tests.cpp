#include "../UEFIExtract/ffsdumper.h"
#include "../common/nvramparser.h"
#include "../common/nvram.h"
#include "../common/brotli/include/brotli/decode.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>

static unsigned failures = 0;
extern "C" int testBrotliBlockSwitch(void);

static void expect(bool condition, const char* name)
{
    if (!condition) {
        std::cerr << "FAIL: " << name << '\n';
        failures++;
    }
}

static bool containsMessage(const NvramParser& parser, const char* text)
{
    for (const auto& message : parser.getMessages())
        if (std::string(message.first.toLocal8Bit()).find(text) != std::string::npos)
            return true;
    return false;
}

static void nvramTest(const UByteArray& body, const char* format, bool failure)
{
    TreeModel model;
    FfsParser ffs(&model);
    NvramParser parser(&model, &ffs);
    const auto root = model.addItem(0, Types::Volume, 0, "Test", "", "",
        UByteArray(8, '\0'), body, UByteArray(), Fixed);
    expect(parser.parseNvramVolumeBody(root) == U_SUCCESS, "NVRAM scanning remains recoverable");
    expect(containsMessage(parser, format) == failure, "recognized corrupt store is distinguished from valid data");
    if (failure) {
        expect(containsMessage(parser, "offset 28h"), "diagnostic includes parent-relative offset");
        expect(containsMessage(parser, "parsing failed:"), "diagnostic includes parser failure reason");
    }
    expect(model.body(root) == body, "NVRAM source bytes remain unchanged");
}

static void nvramTests()
{
    for (bool corrupt : {false, true}) {
        VSS_VARIABLE_STORE_HEADER h = {};
        h.Signature = NVRAM_VSS_STORE_SIGNATURE;
        h.Size = sizeof(h) + 2;
        h.Format = NVRAM_VSS_VARIABLE_STORE_FORMATTED;
        h.State = NVRAM_VSS_VARIABLE_STORE_HEALTHY;
        UByteArray data(32, '\xFF');
        data += UByteArray((const char*)&h, sizeof(h));
        data += corrupt ? UByteArray("\xAA\x00", 2) : UByteArray(2, '\xFF');
        nvramTest(data, "recognized VSS candidate", corrupt);

        VSS2_VARIABLE_STORE_HEADER h2 = {};
        std::memcpy(&h2.Signature, NVRAM_VSS2_STORE_GUID.constData(), sizeof(EFI_GUID));
        h2.Size = sizeof(h2) + 2;
        h2.Format = NVRAM_VSS_VARIABLE_STORE_FORMATTED;
        h2.State = NVRAM_VSS_VARIABLE_STORE_HEALTHY;
        data = UByteArray(32, '\xFF');
        data += UByteArray((const char*)&h2, sizeof(h2));
        data += corrupt ? UByteArray("\xAA\x00", 2) : UByteArray(2, '\xFF');
        nvramTest(data, "recognized VSS2 candidate", corrupt);

        PHOENIX_FLASH_MAP_HEADER flm = {};
        std::memcpy(flm.Signature, NVRAM_PHOENIX_FLASH_MAP_SIGNATURE.constData(), sizeof(flm.Signature));
        flm.NumEntries = corrupt ? 1 : 0;
        data = UByteArray(32, '\xFF') + UByteArray((const char*)&flm, sizeof(flm));
        nvramTest(data, "recognized Phoenix Flash Map candidate", corrupt);
    }
    nvramTest(UByteArray(80, '\xFF'), "parsing failed", false);
    nvramTest(UByteArray(80, '\0'), "parsing failed", false);
    for (const auto& data : {UByteArray("Nope", 4), UByteArray("NVAR\0\0", 6)}) {
        TreeModel model;
        FfsParser ffs(&model);
        NvramParser parser(&model, &ffs);
        const auto root = model.addItem(0, Types::File, 0, "Test", "", "",
            UByteArray(), data, UByteArray(), Fixed);
        expect(parser.parseNvarStore(root, true) == U_INVALID_STORE, "invalid NVAR probe fails");
        expect(containsMessage(parser, "recognized AMI NVAR candidate") == (data.left(4) == UByteArray("NVAR", 4)),
            "NVAR probe reports only a complete recognized signature");
        parser.clearMessages();
        expect(parser.parseNvarStore(root, false) == U_INVALID_STORE, "explicit invalid NVAR parse fails");
        expect(!parser.getMessages().empty(), "explicit NVAR parse never silently loses an error");
        expect(containsMessage(parser, "recognized AMI NVAR candidate") == (data.left(4) == UByteArray("NVAR", 4)),
            "explicit NVAR failure does not manufacture format recognition");
    }
    VSS_VARIABLE_STORE_HEADER h = {};
    h.Signature = NVRAM_VSS_STORE_SIGNATURE;
    h.Size = sizeof(h) + 2;
    h.Format = NVRAM_VSS_VARIABLE_STORE_FORMATTED;
    h.State = NVRAM_VSS_VARIABLE_STORE_HEALTHY;
    UByteArray mixed = UByteArray((const char*)&h, sizeof(h)) + UByteArray("\xAA\x00", 2)
        + UByteArray(16, '\xFF') + UByteArray((const char*)&h, sizeof(h)) + UByteArray(2, '\xFF');
    TreeModel model;
    FfsParser ffs(&model);
    NvramParser parser(&model, &ffs);
    const auto root = model.addItem(0, Types::Volume, 0, "Mixed", "", "",
        UByteArray(), mixed, UByteArray(), Fixed);
    expect(parser.parseNvramVolumeBody(root) == U_SUCCESS, "scanner continues after a corrupt candidate");
    unsigned stores = 0;
    for (int i = 0; i < model.rowCount(root); i++)
        if (model.type(root.child(i, 0)) == Types::VssStore)
            stores++;
    expect(stores == 1 && containsMessage(parser, "recognized VSS candidate"),
        "corrupt candidate is reported while the subsequent valid store is retained");
}

static void extractionTests(const UString& base)
{
    TreeModel model;
    const UByteArray guid = EFI_FIRMWARE_FILE_SYSTEM2_GUID;
    const UString guidText = guidToUString(readUnaligned((const EFI_GUID*)guid.constData()));
    const auto file = model.addItem(0, Types::File, EFI_FV_FILETYPE_FREEFORM, "File", "", "",
        guid, UByteArray("file-body"), UByteArray(), Fixed);
    const auto section = model.addItem(16, Types::Section, EFI_SECTION_GUID_DEFINED, "Compressed", "", "",
        UByteArray(4, '\0'), UByteArray("compressed-body"), UByteArray(), Fixed, file);
    model.setUncompressedData(section, UByteArray("exact decoded bytes"));
    model.addItem(4, Types::Section, EFI_SECTION_RAW, "Leaf", "", "",
        UByteArray(4, '\0'), UByteArray("leaf-body"), UByteArray(), Fixed, section);
    FfsDumper dumper(&model);
    for (const char* suffix : {"/unc-first", "/unc-second"}) {
        const UString path = base + UString(suffix);
        expect(dumper.dump(file, path, FfsDumper::DUMP_UNC_DATA, EFI_SECTION_GUID_DEFINED, guidText) == U_SUCCESS,
            "explicit uncompressed extraction includes matching non-leaf sections");
        UByteArray bytes;
        expect(readFileIntoBuffer(path + UString("/unc_data.bin"), bytes)
            && bytes == model.uncompressedData(section), "uncompressed output bytes and repeated-dump counter are exact");
    }
    const UString bodyPath = base + UString("/body");
    expect(dumper.dump(file, bodyPath, FfsDumper::DUMP_BODY, EFI_SECTION_GUID_DEFINED, guidText) == U_SUCCESS,
        "explicit body extraction includes a matching non-leaf section");
    UByteArray bytes;
    expect(readFileIntoBuffer(bodyPath + UString("/body.bin"), bytes)
        && bytes == model.body(section), "body mode still returns compressed bytes");
    const UString headerPath = base + UString("/header");
    expect(dumper.dump(file, headerPath, FfsDumper::DUMP_HEADER, EFI_SECTION_GUID_DEFINED, guidText) == U_SUCCESS,
        "section-filtered header extraction succeeds with a colliding file subtype");
    expect(readFileIntoBuffer(headerPath + UString("/header.bin"), bytes)
        && bytes == model.header(section) && !isExistOnFs(headerPath + UString("/header_1.bin")),
        "section-filtered headers contain only the matching section");
    const UString infoPath = base + UString("/info");
    expect(dumper.dump(file, infoPath, FfsDumper::DUMP_INFO, EFI_SECTION_GUID_DEFINED, guidText) == U_SUCCESS,
        "section-filtered info extraction succeeds");
    expect(readFileIntoBuffer(infoPath + UString("/info.txt"), bytes)
        && std::string(bytes.constData(), bytes.size()).find("Type: Section") != std::string::npos
        && !isExistOnFs(infoPath + UString("/info_1.txt")), "section-filtered info excludes the parent file");
    const UString filePath = base + UString("/file");
    expect(dumper.dump(file, filePath, FfsDumper::DUMP_FILE, EFI_SECTION_GUID_DEFINED, guidText) == U_SUCCESS,
        "a matching section still permits explicit parent-file extraction");
    expect(readFileIntoBuffer(filePath + UString("/file.ffs"), bytes)
        && bytes == model.header(file) + model.body(file) + model.tail(file),
        "explicit file mode retains the complete parent file");
    expect(dumper.dump(file, base + UString("/wrong-type"), FfsDumper::DUMP_UNC_DATA, EFI_SECTION_RAW, guidText) == U_ITEM_NOT_FOUND,
        "section filter does not accidentally dump a different non-leaf type");
    expect(dumper.dump(file, base + UString("/wrong-guid"), FfsDumper::DUMP_UNC_DATA,
        FfsDumper::IgnoreSectionType, "00000000-0000-0000-0000-000000000000") == U_ITEM_NOT_FOUND,
        "GUID filter safely handles short section headers");
    const UString defaultPath = base + UString("/default");
    expect(dumper.dump(file, defaultPath) == U_SUCCESS, "default leaf dump still succeeds");
    expect(!isExistOnFs(defaultPath + UString("/header.bin")), "default dump omits the non-leaf root");
    expect(!isExistOnFs(defaultPath + UString("/0 Compressed/unc_data.bin")), "default dump omits non-leaf uncompressed data");

    TreeModel noMatch;
    const auto unmatchedFile = noMatch.addItem(0, Types::File, EFI_FV_FILETYPE_FREEFORM, "Unmatched", "", "",
        guid, UByteArray("not-a-guided-section"), UByteArray(), Fixed);
    noMatch.addItem(16, Types::Section, EFI_SECTION_RAW, "Raw", "", "",
        UByteArray(4, '\0'), UByteArray("raw-body"), UByteArray(), Fixed, unmatchedFile);
    FfsDumper unmatchedDumper(&noMatch);
    for (const auto mode : {FfsDumper::DUMP_HEADER, FfsDumper::DUMP_BODY, FfsDumper::DUMP_INFO, FfsDumper::DUMP_FILE}) {
        expect(unmatchedDumper.dump(unmatchedFile, base + usprintf("/no-match-%u", static_cast<unsigned>(mode)),
            mode, EFI_SECTION_GUID_DEFINED, guidText) == U_ITEM_NOT_FOUND,
            "a colliding FFS subtype without a matching section cannot report success");
    }
    const UString unfilteredPath = base + UString("/unfiltered");
    expect(unmatchedDumper.dump(unmatchedFile, unfilteredPath, FfsDumper::DUMP_BODY) == U_SUCCESS,
        "unfiltered explicit extraction still includes non-section items");
    expect(readFileIntoBuffer(unfilteredPath + UString("/body.bin"), bytes)
        && bytes == noMatch.body(unmatchedFile), "unfiltered body mode preserves the non-leaf file bytes");
}

static void brotliTests()
{
    const unsigned char valid[] = {0x1B, 0x3F, 0, 0, 0x24, 0xB0, 0xE2, 0x99, 0x80, 0x12};
    unsigned char decoded[4096] = {};
    size_t length = sizeof(decoded);
    expect(BrotliDecoderDecompress(sizeof(valid), valid, &length, decoded) == BROTLI_DECODER_RESULT_SUCCESS
        && length == 64 && std::memcmp(decoded, std::string(64, 'X').data(), 64) == 0,
        "upstream 64x Brotli vector decodes exactly");
    expect(testBrotliBlockSwitch() == 0, "impossible switches fail in all three decoder block types");
    auto* decoder = BrotliDecoderCreateInstance(NULL, NULL, NULL);
    expect(decoder != NULL, "streaming decoder allocated");
    if (!decoder)
        return;
    unsigned char* out = decoded;
    size_t availableOut = sizeof(decoded);
    for (size_t i = 0; i < sizeof(valid); i++) {
        const unsigned char* in = valid + i;
        size_t availableIn = 1;
        const auto result = BrotliDecoderDecompressStream(decoder, &availableIn, &in, &availableOut, &out, NULL);
        expect(result == (i + 1 == sizeof(valid) ? BROTLI_DECODER_RESULT_SUCCESS : BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT),
            "legitimate incremental input is not confused with a format error");
    }
    expect(out - decoded == 64 && std::memcmp(decoded, std::string(64, 'X').data(), 64) == 0,
        "byte-at-a-time decoding preserves output");
    BrotliDecoderDestroyInstance(decoder);
}

int main(int argc, char** argv)
{
    if (argc != 2)
        return 2;
    const UString parent = getAbsPath(argv[1]);
    if (!isExistOnFs(parent) && !makeDirectory(parent))
        return 3;
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const UString base = parent + usprintf("/run-%lld", static_cast<long long>(stamp));
    if (!makeDirectory(base))
        return 3;
    brotliTests();
    extractionTests(base);
    nvramTests();
    if (!failures)
        std::cout << "Parser regression tests passed\n";
    return failures ? 1 : 0;
}
