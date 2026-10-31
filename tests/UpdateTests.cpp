// Tests for the update check's version handling. Exit code = number of failures.

#include "Version.h"

#include <cstdio>

namespace {

int g_failures = 0;

#define AFM_CHECK(cond)                                                                   \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                                 \
        }                                                                                 \
    } while (0)

using namespace afm::app::version;

void testParse() {
    AFM_CHECK(parse("0.3.1") && (*parse("0.3.1"))[1] == 3);
    AFM_CHECK(parse("v1.20.300") && (*parse("v1.20.300"))[2] == 300);
    for (const char* bad : {"", "v", "1", "1.2", "1.2.", "1..3", "1.2.3.4", "1.2.3-beta", "1.2.x", " 1.2.3", "1.2.3 ",
                            "-1.2.3", "99999999.0.0"}) {
        AFM_CHECK(!parse(bad));
    }
}

void testIsNewer() {
    AFM_CHECK(isNewer("v0.3.2", "0.3.1"));
    AFM_CHECK(isNewer("0.3.10", "0.3.9"));  // numeric, not alphabetical
    AFM_CHECK(isNewer("1.0.0", "0.99.99"));
    AFM_CHECK(!isNewer("0.3.1", "0.3.1"));
    AFM_CHECK(!isNewer("v0.3.0", "0.3.1"));  // an older "latest" never prompts a downgrade
    AFM_CHECK(!isNewer("garbage", "0.3.1"));
    AFM_CHECK(!isNewer("0.4.0", "dev"));
}

void testTagFromJson() {
    AFM_CHECK(tagFromReleaseJson(R"({"url":"x","tag_name":"v0.3.2","name":"AudioFlow"})") == "v0.3.2");
    AFM_CHECK(tagFromReleaseJson("{\n  \"tag_name\" :  \"0.4.0\",\n  \"draft\": false\n}") == "0.4.0");
    AFM_CHECK(tagFromReleaseJson("").empty());
    AFM_CHECK(tagFromReleaseJson(R"({"message":"Not Found"})").empty());
    AFM_CHECK(tagFromReleaseJson(R"({"tag_name":123})").empty());
    AFM_CHECK(tagFromReleaseJson(R"({"tag_name":"v1.0.0/../../evil"})").empty());  // never ends up in a URL
    AFM_CHECK(tagFromReleaseJson(R"({"tag_name":"v0.3.2)").empty());              // truncated response
    AFM_CHECK(tagFromReleaseJson(R"({"tag_name":"nightly"})").empty());
}

} // namespace

int main() {
    testParse();
    testIsNewer();
    testTagFromJson();
    if (g_failures == 0) std::printf("All update tests passed.\n");
    return g_failures;
}
