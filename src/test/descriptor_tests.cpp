// Copyright (c) 2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <vector>
#include <string>
#include <script/sign.h>
#include <script/standard.h>
#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>
#include <script/descriptor.h>
#include <utilstrencodings.h>

namespace {

void CheckUnparsable(const std::string& prv, const std::string& pub)
{
    FlatSigningProvider keys_priv, keys_pub;
    auto parse_priv = Parse(prv, keys_priv);
    auto parse_pub = Parse(pub, keys_pub);
    BOOST_CHECK(!parse_priv);
    BOOST_CHECK(!parse_pub);
}

constexpr int DEFAULT = 0;
constexpr int RANGE = 1; // Expected to be ranged descriptor
constexpr int HARDENED = 2; // Derivation needs access to private keys
constexpr int UNSOLVABLE = 4; // This descriptor is not expected to be solvable
constexpr int SIGNABLE = 8; // We can sign with this descriptor (this is not true when actual BIP32 derivation is used, as that's not integrated in our signing code)

std::string MaybeUseHInsteadOfApostrophy(std::string ret)
{
    if (InsecureRandBool()) {
        while (true) {
            auto it = ret.find("'");
            if (it != std::string::npos) {
                ret[it] = 'h';
            } else {
                break;
            }
        }
    }
    return ret;
}

void Check(const std::string& prv, const std::string& pub, int flags, const std::vector<std::vector<std::string>>& scripts)
{
    FlatSigningProvider keys_priv, keys_pub;

    // Check that parsing succeeds.
    auto parse_priv = Parse(MaybeUseHInsteadOfApostrophy(prv), keys_priv);
    auto parse_pub = Parse(MaybeUseHInsteadOfApostrophy(pub), keys_pub);
    BOOST_CHECK(parse_priv);
    BOOST_CHECK(parse_pub);

    // Check private keys are extracted from the private version but not the public one.
    BOOST_CHECK(keys_priv.keys.size());
    BOOST_CHECK(!keys_pub.keys.size());

    // Check that both versions serialize back to the public version.
    std::string pub1 = parse_priv->ToString();
    std::string pub2 = parse_priv->ToString();
    BOOST_CHECK_EQUAL(pub, pub1);
    BOOST_CHECK_EQUAL(pub, pub2);

    // Check that both can be serialized with private key back to the private version, but not without private key.
    std::string prv1;
    BOOST_CHECK(parse_priv->ToPrivateString(keys_priv, prv1));
    BOOST_CHECK_EQUAL(prv, prv1);
    BOOST_CHECK(!parse_priv->ToPrivateString(keys_pub, prv1));
    BOOST_CHECK(parse_pub->ToPrivateString(keys_priv, prv1));
    BOOST_CHECK_EQUAL(prv, prv1);
    BOOST_CHECK(!parse_pub->ToPrivateString(keys_pub, prv1));

    // Check whether IsRange on both returns the expected result
    BOOST_CHECK_EQUAL(parse_pub->IsRange(), (flags & RANGE) != 0);
    BOOST_CHECK_EQUAL(parse_priv->IsRange(), (flags & RANGE) != 0);


    // Is not ranged descriptor, only a single result is expected.
    if (!(flags & RANGE)) assert(scripts.size() == 1);

    size_t max = (flags & RANGE) ? scripts.size() : 3;
    for (size_t i = 0; i < max; ++i) {
        const auto& ref = scripts[(flags & RANGE) ? i : 0];
        for (int t = 0; t < 2; ++t) {
            FlatSigningProvider key_provider = (flags & HARDENED) ? keys_priv : keys_pub;
            FlatSigningProvider script_provider;
            std::vector<CScript> spks;
            BOOST_CHECK((t ? parse_priv : parse_pub)->Expand(i, key_provider, spks, script_provider));
            BOOST_CHECK_EQUAL(spks.size(), ref.size());
            for (size_t n = 0; n < spks.size(); ++n) {
                BOOST_CHECK_EQUAL(ref[n], HexStr(spks[n].begin(), spks[n].end()));
                BOOST_CHECK_EQUAL(IsSolvable(Merge(key_provider, script_provider), spks[n]), (flags & UNSOLVABLE) == 0);

                if (flags & SIGNABLE) {
                    CMutableTransaction spend;
                    spend.vin.resize(1);
                    spend.vout.resize(1);
                    BOOST_CHECK_MESSAGE(SignSignature(Merge(keys_priv, script_provider), spks[n], spend, 0, 1, SIGHASH_ALL), prv);
                }
            }

        }
    }
}

}

BOOST_FIXTURE_TEST_SUITE(descriptor_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(descriptor_test)
{
    // Basic single-key compressed
    Check("combo(LN1aAHwgYpV2sZvmCxJzk5PXA7mnhT3t9P9eT6JPSzVZ2XCkFCwF)", "combo(03a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd)", SIGNABLE, {{"2103a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bdac","76a9149a1c78a507689f6f54b847ad1cef1e614ee23f1e88ac","00149a1c78a507689f6f54b847ad1cef1e614ee23f1e","a91484ab21b1b2fd065d4504ff693d832434b6108d7b87"}});
    Check("pk(LN1aAHwgYpV2sZvmCxJzk5PXA7mnhT3t9P9eT6JPSzVZ2XCkFCwF)", "pk(03a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd)", SIGNABLE, {{"2103a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bdac"}});
    Check("pkh(LN1aAHwgYpV2sZvmCxJzk5PXA7mnhT3t9P9eT6JPSzVZ2XCkFCwF)", "pkh(03a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd)", SIGNABLE, {{"76a9149a1c78a507689f6f54b847ad1cef1e614ee23f1e88ac"}});
    Check("wpkh(LN1aAHwgYpV2sZvmCxJzk5PXA7mnhT3t9P9eT6JPSzVZ2XCkFCwF)", "wpkh(03a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd)", SIGNABLE, {{"00149a1c78a507689f6f54b847ad1cef1e614ee23f1e"}});
    Check("sh(wpkh(LN1aAHwgYpV2sZvmCxJzk5PXA7mnhT3t9P9eT6JPSzVZ2XCkFCwF))", "sh(wpkh(03a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd))", SIGNABLE, {{"a91484ab21b1b2fd065d4504ff693d832434b6108d7b87"}});

    // Basic single-key uncompressed
    Check("combo(5PS3yw8rk1LVX32JigGQWF8pcbcXHsJsZFzEvVPX1TAeASgvZPZ)", "combo(04a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd5b8dec5235a0fa8722476c7709c02559e3aa73aa03918ba2d492eea75abea235)", SIGNABLE, {{"4104a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd5b8dec5235a0fa8722476c7709c02559e3aa73aa03918ba2d492eea75abea235ac","76a914b5bd079c4d57cc7fc28ecf8213a6b791625b818388ac"}});
    Check("pk(5PS3yw8rk1LVX32JigGQWF8pcbcXHsJsZFzEvVPX1TAeASgvZPZ)", "pk(04a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd5b8dec5235a0fa8722476c7709c02559e3aa73aa03918ba2d492eea75abea235)", SIGNABLE, {{"4104a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd5b8dec5235a0fa8722476c7709c02559e3aa73aa03918ba2d492eea75abea235ac"}});
    Check("pkh(5PS3yw8rk1LVX32JigGQWF8pcbcXHsJsZFzEvVPX1TAeASgvZPZ)", "pkh(04a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd5b8dec5235a0fa8722476c7709c02559e3aa73aa03918ba2d492eea75abea235)", SIGNABLE, {{"76a914b5bd079c4d57cc7fc28ecf8213a6b791625b818388ac"}});
    CheckUnparsable("wpkh(5PS3yw8rk1LVX32JigGQWF8pcbcXHsJsZFzEvVPX1TAeASgvZPZ)", "wpkh(04a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd5b8dec5235a0fa8722476c7709c02559e3aa73aa03918ba2d492eea75abea235)"); // No uncompressed keys in witness
    CheckUnparsable("wsh(pk(5PS3yw8rk1LVX32JigGQWF8pcbcXHsJsZFzEvVPX1TAeASgvZPZ))", "wsh(pk(04a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd5b8dec5235a0fa8722476c7709c02559e3aa73aa03918ba2d492eea75abea235))"); // No uncompressed keys in witness
    CheckUnparsable("sh(wpkh(5PS3yw8rk1LVX32JigGQWF8pcbcXHsJsZFzEvVPX1TAeASgvZPZ))", "sh(wpkh(04a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd5b8dec5235a0fa8722476c7709c02559e3aa73aa03918ba2d492eea75abea235))"); // No uncompressed keys in witness

    // Some unconventional single-key constructions
    Check("sh(pk(LN1aAHwgYpV2sZvmCxJzk5PXA7mnhT3t9P9eT6JPSzVZ2XCkFCwF))", "sh(pk(03a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd))", SIGNABLE, {{"a9141857af51a5e516552b3086430fd8ce55f7c1a52487"}});
    Check("sh(pkh(LN1aAHwgYpV2sZvmCxJzk5PXA7mnhT3t9P9eT6JPSzVZ2XCkFCwF))", "sh(pkh(03a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd))", SIGNABLE, {{"a9141a31ad23bf49c247dd531a623c2ef57da3c400c587"}});
    Check("wsh(pk(LN1aAHwgYpV2sZvmCxJzk5PXA7mnhT3t9P9eT6JPSzVZ2XCkFCwF))", "wsh(pk(03a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd))", SIGNABLE, {{"00202e271faa2325c199d25d22e1ead982e45b64eeb4f31e73dbdf41bd4b5fec23fa"}});
    Check("wsh(pkh(LN1aAHwgYpV2sZvmCxJzk5PXA7mnhT3t9P9eT6JPSzVZ2XCkFCwF))", "wsh(pkh(03a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd))", SIGNABLE, {{"0020338e023079b91c58571b20e602d7805fb808c22473cbc391a41b1bd3a192e75b"}});
    Check("sh(wsh(pk(LN1aAHwgYpV2sZvmCxJzk5PXA7mnhT3t9P9eT6JPSzVZ2XCkFCwF)))", "sh(wsh(pk(03a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd)))", SIGNABLE, {{"a91472d0c5a3bfad8c3e7bd5303a72b94240e80b6f1787"}});
    Check("sh(wsh(pkh(LN1aAHwgYpV2sZvmCxJzk5PXA7mnhT3t9P9eT6JPSzVZ2XCkFCwF)))", "sh(wsh(pkh(03a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd)))", SIGNABLE, {{"a914b61b92e2ca21bac1e72a3ab859a742982bea960a87"}});

    // Versions with BIP32 derivations
    Check("combo(xprvA1RpRA33e1JQ7ifknakTFpgNXPmW2YvmhqLQYMmrj4xJXXWYpDPS3xz7iAxn8L39njGVyuoseXzU6rcxFLJ8HFsTjSyQbLYnMpCqE2VbFWc)", "combo(xpub6ERApfZwUNrhLCkDtcHTcxd75RbzS1ed54G1LkBUHQVHQKqhMkhgbmJbZRkrgZw4koxb5JaHWkY4ALHY2grBGRjaDMzQLcgJvLJuZZvRcEL)", SIGNABLE, {{"2102d2b36900396c9282fa14628566582f206a5dd0bcc8d5e892611806cafb0301f0ac","76a91431a507b815593dfc51ffc7245ae7e5aee304246e88ac","001431a507b815593dfc51ffc7245ae7e5aee304246e","a9142aafb926eb247cb18240a7f4c07983ad1f37922687"}});
    Check("pk(xprv9uPDJpEQgRQfDcW7BkF7eTya6RPxXeJCqCJGHuCJ4GiRVLzkTXBAJMu2qaMWPrS7AANYqdq6vcBcBUdJCVVFceUvJFjaPdGZ2y9WACViL4L/0)", "pk(xpub68NZiKmJWnxxS6aaHmn81bvJeTESw724CRDs6HbuccFQN9Ku14VQrADWgqbhhTHBaohPX4CjNLf9fq9MYo6oDaPPLPxSb7gwQN3ih19Zm4Y/0)", DEFAULT, {{"210379e45b3cf75f9c5f9befd8e9506fb962f6a9d185ac87001ec44a8d3df8d4a9e3ac"}});
    Check("pkh(xprv9s21ZrQH143K31xYSDQpPDxsXRTUcvj2iNHm5NUtrGiGG5e2DtALGdso3pGz6ssrdK4PFmM8NSpSBHNqPqm55Qn3LqFtT2emdEXVYsCzC2U/2147483647'/0)", "pkh(xpub661MyMwAqRbcFW31YEwpkMuc5THy2PSt5bDMsktWQcFF8syAmRUapSCGu8ED9W6oDMSgv6Zz8idoc4a6mr8BDzTJY47LJhkJ8UB7WEGuduB/2147483647'/0)", HARDENED, {{"76a914ebdc90806a9c4356c1c88e42216611e1cb4c1c1788ac"}});
    Check("wpkh(xprv9vHkqa6EV4sPZHYqZznhT2NPtPCjKuDKGY38FBWLvgaDx45zo9WQRUT3dKYnjwih2yJD9mkrocEZXo1ex8G81dwSM1fwqWpWkeS3v86pgKt/1/2/*)", "wpkh(xpub69H7F5d8KSRgmmdJg2KhpAK8SR3DjMwAdkxj3ZuxV27CprR9LgpeyGmXUbC6wb7ERfvrnKZjXoUmmDznezpbZb7ap6r1D3tgFxHmwMkQTPH/1/2/*)", RANGE, {{"0014326b2249e3a25d5dc60935f044ee835d090ba859"},{"0014af0bd98abc2f2cae66e36896a39ffe2d32984fb7"},{"00141fa798efd1cbf95cebf912c031b8a4a6e9fb9f27"}});
    Check("sh(wpkh(xprv9s21ZrQH143K3QTDL4LXw2F7HEK3wJUD2nW2nRk4stbPy6cq3jPPqjiChkVvvNKmPGJxWUtg6LnF5kejMRNNU3TGtRBeJgk33yuGBxrMPHi/10/20/30/40/*'))", "sh(wpkh(xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8/10/20/30/40/*'))", RANGE | HARDENED, {{"a9149a4d9901d6af519b2a23d4a2f51650fcba87ce7b87"},{"a914bed59fc0024fae941d6e20a3b44a109ae740129287"},{"a9148483aa1116eb9c05c482a72bada4b1db24af654387"}});
    Check("combo(xprvA2JDeKCSNNZky6uBCviVfJSKyQ1mDYahRjijr5idH2WwLsEd4Hsb2Tyh8RfQMuPh7f7RtyzTtdrbdqqsunu5Mm3wDvUAKRHSC34sJ7in334/*)", "combo(xpub6FHa3pjLCk84BayeJxFW2SP4XRrFd1JYnxeLeU8EqN3vDfZmbqBqaGJAyiLjTAwm6ZLRQUMv1ZACTj37sR62cfN7fe5JnJ7dh8zL4fiyLHV/*)", RANGE, {{"2102df12b7035bdac8e3bab862a3a83d06ea6b17b6753d52edecba9be46f5d09e076ac","76a914f90e3178ca25f2c808dc76624032d352fdbdfaf288ac","0014f90e3178ca25f2c808dc76624032d352fdbdfaf2","a91408f3ea8c68d4a7585bf9e8bda226723f70e445f087"},{"21032869a233c9adff9a994e4966e5b821fd5bac066da6c3112488dc52383b4a98ecac","76a914a8409d1b6dfb1ed2a3e8aa5e0ef2ff26b15b75b788ac","0014a8409d1b6dfb1ed2a3e8aa5e0ef2ff26b15b75b7","a91473e39884cb71ae4e5ac9739e9225026c99763e6687"}});
    CheckUnparsable("pkh(xprv9s21ZrQH143K31xYSDQpPDxsXRTUcvj2iNHm5NUtrGiGG5e2DtALGdso3pGz6ssrdK4PFmM8NSpSBHNqPqm55Qn3LqFtT2emdEXVYsCzC2U/2147483648)", "pkh(xpub661MyMwAqRbcFW31YEwpkMuc5THy2PSt5bDMsktWQcFF8syAmRUapSCGu8ED9W6oDMSgv6Zz8idoc4a6mr8BDzTJY47LJhkJ8UB7WEGuduB/2147483648)"); // BIP 32 path element overflow

    // Multisig constructions
    Check("multi(1,LN1aAHwgYpV2sZvmCxJzk5PXA7mnhT3t9P9eT6JPSzVZ2XCkFCwF,5PS3yw8rk1LVX32JigGQWF8pcbcXHsJsZFzEvVPX1TAeASgvZPZ)", "multi(1,03a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd,04a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd5b8dec5235a0fa8722476c7709c02559e3aa73aa03918ba2d492eea75abea235)", SIGNABLE, {{"512103a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd4104a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd5b8dec5235a0fa8722476c7709c02559e3aa73aa03918ba2d492eea75abea23552ae"}});
    Check("sh(multi(2,xprvA1RpRA33e1JQ7ifknakTFpgNXPmW2YvmhqLQYMmrj4xJXXWYpDPS3xz7iAxn8L39njGVyuoseXzU6rcxFLJ8HFsTjSyQbLYnMpCqE2VbFWc,xprv9uPDJpEQgRQfDcW7BkF7eTya6RPxXeJCqCJGHuCJ4GiRVLzkTXBAJMu2qaMWPrS7AANYqdq6vcBcBUdJCVVFceUvJFjaPdGZ2y9WACViL4L/0))", "sh(multi(2,xpub6ERApfZwUNrhLCkDtcHTcxd75RbzS1ed54G1LkBUHQVHQKqhMkhgbmJbZRkrgZw4koxb5JaHWkY4ALHY2grBGRjaDMzQLcgJvLJuZZvRcEL,xpub68NZiKmJWnxxS6aaHmn81bvJeTESw724CRDs6HbuccFQN9Ku14VQrADWgqbhhTHBaohPX4CjNLf9fq9MYo6oDaPPLPxSb7gwQN3ih19Zm4Y/0))", DEFAULT, {{"a91445a9a622a8b0a1269944be477640eedc447bbd8487"}});
    Check("wsh(multi(2,xprv9s21ZrQH143K31xYSDQpPDxsXRTUcvj2iNHm5NUtrGiGG5e2DtALGdso3pGz6ssrdK4PFmM8NSpSBHNqPqm55Qn3LqFtT2emdEXVYsCzC2U/2147483647'/0,xprv9vHkqa6EV4sPZHYqZznhT2NPtPCjKuDKGY38FBWLvgaDx45zo9WQRUT3dKYnjwih2yJD9mkrocEZXo1ex8G81dwSM1fwqWpWkeS3v86pgKt/1/2/*,xprv9s21ZrQH143K3QTDL4LXw2F7HEK3wJUD2nW2nRk4stbPy6cq3jPPqjiChkVvvNKmPGJxWUtg6LnF5kejMRNNU3TGtRBeJgk33yuGBxrMPHi/10/20/30/40/*'))", "wsh(multi(2,xpub661MyMwAqRbcFW31YEwpkMuc5THy2PSt5bDMsktWQcFF8syAmRUapSCGu8ED9W6oDMSgv6Zz8idoc4a6mr8BDzTJY47LJhkJ8UB7WEGuduB/2147483647'/0,xpub69H7F5d8KSRgmmdJg2KhpAK8SR3DjMwAdkxj3ZuxV27CprR9LgpeyGmXUbC6wb7ERfvrnKZjXoUmmDznezpbZb7ap6r1D3tgFxHmwMkQTPH/1/2/*,xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8/10/20/30/40/*'))", HARDENED | RANGE, {{"0020b92623201f3bb7c3771d45b2ad1d0351ea8fbf8cfe0a0e570264e1075fa1948f"},{"002036a08bbe4923af41cf4316817c93b8d37e2f635dd25cfff06bd50df6ae7ea203"},{"0020a96e7ab4607ca6b261bfe3245ffda9c746b28d3f59e83d34820ec0e2b36c139c"}});
    Check("sh(wsh(multi(16,LHxS8PvP8tohGgyamb8BcY8HKEWmjsE2FqcEs1pyCJoLQWrJqKWA,LERe8RFzYiipsJ1tzzysoQ5HxC2UrSLpBDi8rkiZXShN9UcM7gAF,LFxwh2SB2FoBpyx4bEtRrKWahhARJPevAAk72dyMUSEjXe5z2NyS,LKLjWxdFoHQDWmxuQ8aC3bC3GnSDerut9y2LzTCea1mBKSF47JKj,LJy1Sb158KaXgfYpS7KXXqgsG4kih8hXjkqaKMWda4w4yQ6MZt3k,LFNTWmAFTP7XLcXcWUcmtEMx54oVnegx7vVoncj63b2dab9VtDxr,LNotZ3yh94Sw2WY1XvtGWFzEMSzytF1jxataKuR8M1XzubkbaeyH,LHQPcpyQaVwTVVrfNwok64xLF6BH3P3DrNKec286Cg8LjU5MhMzB,LLwYcvX7cLmLSUKcqhLrnXY7cg3cmPSnFYpDyZMpRejdBPi1RYzp,LGtYwMpRs4NyfoMJbeZC4BEejJKMVdNcPqVTwqAsaDij9HR8GvpX,LF2w7c3DpmPfMw1QUG8qUuFF2LCDUkcykZytMwDPF6Azj49oHxWA,LHMjjbRAVe8yagHCSJnCGoVLEiMvr9n75JWpdvktWbBcvsCAzy4N,LHr5VWfkDXBDhcDcb1YyGYjYg5RpMPeqWgWoeKpWS5BciovrF2ZE,LGLnbigroRNY1sBRiK8pw77dvgcKeRppFVYHmcGhWXxGCvv4G6Cs,LHTUnUSjejEAGR3n8Kb9hLNfVJXDPjiQ2PSvPY4qXFYokfhMN3ET,LK7rSBgn9AfcGnq5iDdgPkavbnNPWP4sQSTocSsdQihueVCK8Ro2)))","sh(wsh(multi(16,03669b8afcec803a0d323e9a17f3ea8e68e8abe5a278020a929adbec52421adbd0,0260b2003c386519fc9eadf2b5cf124dd8eea4c4e68d5e154050a9346ea98ce600,0362a74e399c39ed5593852a30147f2959b56bb827dfa3e60e464b02ccf87dc5e8,0261345b53de74a4d721ef877c255429961b7e43714171ac06168d7e08c542a8b8,02da72e8b46901a65d4374fe6315538d8f368557dda3a1dcf9ea903f3afe7314c8,0318c82dd0b53fd3a932d16e0ba9e278fcc937c582d5781be626ff16e201f72286,0297ccef1ef99f9d73dec9ad37476ddb232f1238aff877af19e72ba04493361009,02e502cfd5c3f972fe9a3e2a18827820638f96b6f347e54d63deb839011fd5765d,03e687710f0e3ebe81c1037074da939d409c0025f17eb86adb9427d28f0f7ae0e9,02c04d3a5274952acdbc76987f3184b346a483d43be40874624b29e3692c1df5af,02ed06e0f418b5b43a7ec01d1d7d27290fa15f75771cb69b642a51471c29c84acd,036d46073cbb9ffee90473f3da429abc8de7f8751199da44485682a989a4bebb24,02f5d1ff7c9029a80a4e36b9a5497027ef7f3e73384a4a94fbfe7c4e9164eec8bc,02e41deffd1b7cce11cde209a781adcffdabd1b91c0ba0375857a2bfd9302419f3,02d76625f7956a7fc505ab02556c23ee72d832f1bac391bcd2d3abce5710a13d06,0399eb0a5487515802dc14544cf10b3666623762fbed2ec38a3975716e2c29c232)))", SIGNABLE, {{"a9147fc63e13dc25e8a95a3cee3d9a714ac3afd96f1e87"}});
    CheckUnparsable("sh(multi(16,LHxS8PvP8tohGgyamb8BcY8HKEWmjsE2FqcEs1pyCJoLQWrJqKWA,LERe8RFzYiipsJ1tzzysoQ5HxC2UrSLpBDi8rkiZXShN9UcM7gAF,LFxwh2SB2FoBpyx4bEtRrKWahhARJPevAAk72dyMUSEjXe5z2NyS,LKLjWxdFoHQDWmxuQ8aC3bC3GnSDerut9y2LzTCea1mBKSF47JKj,LJy1Sb158KaXgfYpS7KXXqgsG4kih8hXjkqaKMWda4w4yQ6MZt3k,LFNTWmAFTP7XLcXcWUcmtEMx54oVnegx7vVoncj63b2dab9VtDxr,LNotZ3yh94Sw2WY1XvtGWFzEMSzytF1jxataKuR8M1XzubkbaeyH,LHQPcpyQaVwTVVrfNwok64xLF6BH3P3DrNKec286Cg8LjU5MhMzB,LLwYcvX7cLmLSUKcqhLrnXY7cg3cmPSnFYpDyZMpRejdBPi1RYzp,LGtYwMpRs4NyfoMJbeZC4BEejJKMVdNcPqVTwqAsaDij9HR8GvpX,LF2w7c3DpmPfMw1QUG8qUuFF2LCDUkcykZytMwDPF6Azj49oHxWA,LHMjjbRAVe8yagHCSJnCGoVLEiMvr9n75JWpdvktWbBcvsCAzy4N,LHr5VWfkDXBDhcDcb1YyGYjYg5RpMPeqWgWoeKpWS5BciovrF2ZE,LGLnbigroRNY1sBRiK8pw77dvgcKeRppFVYHmcGhWXxGCvv4G6Cs,LHTUnUSjejEAGR3n8Kb9hLNfVJXDPjiQ2PSvPY4qXFYokfhMN3ET,LK7rSBgn9AfcGnq5iDdgPkavbnNPWP4sQSTocSsdQihueVCK8Ro2))","sh(multi(16,03669b8afcec803a0d323e9a17f3ea8e68e8abe5a278020a929adbec52421adbd0,0260b2003c386519fc9eadf2b5cf124dd8eea4c4e68d5e154050a9346ea98ce600,0362a74e399c39ed5593852a30147f2959b56bb827dfa3e60e464b02ccf87dc5e8,0261345b53de74a4d721ef877c255429961b7e43714171ac06168d7e08c542a8b8,02da72e8b46901a65d4374fe6315538d8f368557dda3a1dcf9ea903f3afe7314c8,0318c82dd0b53fd3a932d16e0ba9e278fcc937c582d5781be626ff16e201f72286,0297ccef1ef99f9d73dec9ad37476ddb232f1238aff877af19e72ba04493361009,02e502cfd5c3f972fe9a3e2a18827820638f96b6f347e54d63deb839011fd5765d,03e687710f0e3ebe81c1037074da939d409c0025f17eb86adb9427d28f0f7ae0e9,02c04d3a5274952acdbc76987f3184b346a483d43be40874624b29e3692c1df5af,02ed06e0f418b5b43a7ec01d1d7d27290fa15f75771cb69b642a51471c29c84acd,036d46073cbb9ffee90473f3da429abc8de7f8751199da44485682a989a4bebb24,02f5d1ff7c9029a80a4e36b9a5497027ef7f3e73384a4a94fbfe7c4e9164eec8bc,02e41deffd1b7cce11cde209a781adcffdabd1b91c0ba0375857a2bfd9302419f3,02d76625f7956a7fc505ab02556c23ee72d832f1bac391bcd2d3abce5710a13d06,0399eb0a5487515802dc14544cf10b3666623762fbed2ec38a3975716e2c29c232))"); // P2SH does not fit 16 compressed pubkeys in a redeemscript

    // Check for invalid nesting of structures
    CheckUnparsable("sh(LN1aAHwgYpV2sZvmCxJzk5PXA7mnhT3t9P9eT6JPSzVZ2XCkFCwF)", "sh(03a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd)"); // P2SH needs a script, not a key
    CheckUnparsable("sh(combo(LN1aAHwgYpV2sZvmCxJzk5PXA7mnhT3t9P9eT6JPSzVZ2XCkFCwF))", "sh(combo(03a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd))"); // Old must be top level
    CheckUnparsable("wsh(LN1aAHwgYpV2sZvmCxJzk5PXA7mnhT3t9P9eT6JPSzVZ2XCkFCwF)", "wsh(03a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd)"); // P2WSH needs a script, not a key
    CheckUnparsable("wsh(wpkh(LN1aAHwgYpV2sZvmCxJzk5PXA7mnhT3t9P9eT6JPSzVZ2XCkFCwF))", "wsh(wpkh(03a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd))"); // Cannot embed witness inside witness
    CheckUnparsable("wsh(sh(pk(LN1aAHwgYpV2sZvmCxJzk5PXA7mnhT3t9P9eT6JPSzVZ2XCkFCwF)))", "wsh(sh(pk(03a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd)))"); // Cannot embed P2SH inside P2WSH
    CheckUnparsable("sh(sh(pk(LN1aAHwgYpV2sZvmCxJzk5PXA7mnhT3t9P9eT6JPSzVZ2XCkFCwF)))", "sh(sh(pk(03a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd)))"); // Cannot embed P2SH inside P2SH
    CheckUnparsable("wsh(wsh(pk(LN1aAHwgYpV2sZvmCxJzk5PXA7mnhT3t9P9eT6JPSzVZ2XCkFCwF)))", "wsh(wsh(pk(03a34b99f22c790c4e36b2b3c2c35a36db06226e41c692fc82b8b56ac1c540c5bd)))"); // Cannot embed P2WSH inside P2WSH
}

BOOST_AUTO_TEST_SUITE_END()
