// What the update manifest is allowed to say. The refusals are what matter: the file it names
// replaces the client.
#include "citron/lite_manifest.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

const std::string kHash = "eb9173a826a54589bbdbbf4d376f29dc0786e5fdc517982627f9643738f824df";

std::string Manifest(const std::string& url, const std::string& sha = kHash,
                     const std::string& version = "103", const std::string& name = "1.0.3") {
    return "{\n \"version\": " + version + ",\n \"versionName\": \"" + name +
           "\",\n \"sha256\": \"" + sha + "\",\n \"url\": \"" + url + "\"\n}";
}

// Every one of these must be refused, and the reason is the point of the test, not the parse.
void Rejects(const std::string& text, const char* why) {
    citron::updater::Release release;
    std::string error;
    Require(!citron::updater::ParseManifest(text, release, error), why);
    Require(!error.empty(), "a refusal must say something the user can read");
}

} // namespace

int main() {
    using citron::updater::ParseManifest;
    using citron::updater::Release;

    // The manifest the publish script writes.
    {
        Release release;
        std::string error;
        Require(ParseManifest(Manifest("https://citron.lol/CitronLite.exe"),
                              release, error),
                "the published manifest shape was rejected");
        Require(release.version == 103, "version was misread");
        Require(release.name == "1.0.3", "versionName was misread");
        Require(release.sha256 == kHash, "sha256 was misread");
        Require(release.path == "/CitronLite.exe", "the download path was misread");
    }

    // Whitespace and key order are the server's business, not ours.
    {
        Release release;
        std::string error;
        Require(ParseManifest("{\"url\":\"https://citron.lol/a/b/Lite.exe\","
                              "\"sha256\" : \"" + kHash + "\",\"versionName\":\"2.0\","
                              "\"version\"\t:\t2000}",
                              release, error),
                "reordered or tightly packed JSON was rejected");
        Require(release.path == "/a/b/Lite.exe", "a nested download path was misread");
        Require(release.version == 2000, "version was misread");
    }

    // A download from anywhere but the update host.
    Rejects(Manifest("https://example.com/CitronLite.exe"), "an off-host download was accepted");
    Rejects(Manifest("http://citron.lol/CitronLite.exe"),
            "a plain-http download was accepted");
    Rejects(Manifest("https://citron.lol.evil.test/CitronLite.exe"),
            "a lookalike hostname was accepted");
    Rejects(Manifest("https://citron.lol/"), "an empty download path was accepted");
    Rejects(Manifest("https://citron.lol/../../etc/passwd"),
            "a traversing download path was accepted");

    // Fields that are missing, the wrong type, or not what they claim to be.
    Rejects("{}", "an empty manifest was accepted");
    Rejects("not json at all", "garbage was accepted");
    Rejects(Manifest("https://citron.lol/CitronLite.exe", "deadbeef"),
            "a short hash was accepted");
    Rejects(Manifest("https://citron.lol/CitronLite.exe",
                     "EB9173A826A54589BBDBBF4D376F29DC0786E5FDC517982627F9643738F824DF"),
            "an uppercase hash was accepted");
    Rejects(Manifest("https://citron.lol/CitronLite.exe", kHash, "0"),
            "version zero was accepted");
    Rejects(Manifest("https://citron.lol/CitronLite.exe", kHash, "\"103\""),
            "a quoted version was accepted");
    Rejects(Manifest("https://citron.lol/CitronLite.exe", kHash, "103", ""),
            "an empty version name was accepted");
    Rejects("{\"version\":103,\"versionName\":\"1.0.3\",\"sha256\":\"" + kHash + "\"}",
            "a manifest with no url was accepted");

    std::cout << "lite manifest tests passed\n";
    return 0;
}
