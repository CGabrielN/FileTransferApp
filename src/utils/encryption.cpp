#include "encryption.hpp"
#include <mbedtls/aes.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/md.h>
#include <mbedtls/gcm.h>
#include <mbedtls/pkcs5.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <cstring>

namespace utils {

    // Static initialization flag
    bool Encryption::s_initialized = false;

    // Static key and IV sizes for AES-256-GCM
    constexpr int KEY_SIZE = 32;  // 256 bits
    constexpr int IV_SIZE = 12;   // 96 bits (recommended for GCM)
    constexpr int TAG_SIZE = 16;  // 128 bits authentication tag

    void Encryption::init() {
        if (s_initialized) {
            return;
        }

        // MbedTLS doesn't require explicit initialization for most operations
        s_initialized = true;
        SPDLOG_INFO("Encryption system initialized (MbedTLS)");
    }

    void Encryption::shutdown() {
        if (!s_initialized) {
            return;
        }

        // MbedTLS doesn't require explicit cleanup for most operations
        s_initialized = false;
        SPDLOG_INFO("Encryption system shutdown");
    }

    bool Encryption::encrypt(const std::vector<uint8_t>& plaintext,
                             const std::string& password,
                             std::vector<uint8_t>& ciphertext) {
        if (!s_initialized) {
            init();
        }

        try {
            // Generate a random salt for key derivation
            std::vector<uint8_t> salt(8);
            mbedtls_entropy_context entropy;
            mbedtls_ctr_drbg_context ctr_drbg;
            const char *pers = "mbedtls_encryption";

            mbedtls_entropy_init(&entropy);
            mbedtls_ctr_drbg_init(&ctr_drbg);

            if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                      (const unsigned char *)pers, strlen(pers)) != 0) {
                SPDLOG_ERROR("Failed to seed random generator");
                mbedtls_entropy_free(&entropy);
                mbedtls_ctr_drbg_free(&ctr_drbg);
                return false;
            }

            if (mbedtls_ctr_drbg_random(&ctr_drbg, salt.data(), salt.size()) != 0) {
                SPDLOG_ERROR("Failed to generate random salt");
                mbedtls_entropy_free(&entropy);
                mbedtls_ctr_drbg_free(&ctr_drbg);
                return false;
            }

            // Derive key and IV from password and salt
            std::vector<uint8_t> key(KEY_SIZE);
            std::vector<uint8_t> iv(IV_SIZE);
            if (!deriveKeyAndIV(password, salt, key, iv)) {
                SPDLOG_ERROR("Failed to derive key and IV");
                mbedtls_entropy_free(&entropy);
                mbedtls_ctr_drbg_free(&ctr_drbg);
                return false;
            }

            // Prepare output buffer:
            // Structure: 8-byte salt + 12-byte IV + ciphertext + 16-byte GCM tag
            ciphertext.clear();
            ciphertext.reserve(salt.size() + iv.size() + plaintext.size() + TAG_SIZE + 16); // Some extra space

            // Add salt and IV to ciphertext
            ciphertext.insert(ciphertext.end(), salt.begin(), salt.end());
            ciphertext.insert(ciphertext.end(), iv.begin(), iv.end());

            // Initialize GCM context
            mbedtls_gcm_context gcm;
            mbedtls_gcm_init(&gcm);

            if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key.data(), key.size() * 8) != 0) {
                SPDLOG_ERROR("Failed to set GCM key");
                mbedtls_gcm_free(&gcm);
                mbedtls_entropy_free(&entropy);
                mbedtls_ctr_drbg_free(&ctr_drbg);
                return false;
            }

            // Reserve space for the ciphertext and tag
            size_t ciphertextOffset = ciphertext.size();
            ciphertext.resize(ciphertextOffset + plaintext.size());
            std::vector<uint8_t> tag(TAG_SIZE);

            // Encrypt
            if (mbedtls_gcm_crypt_and_tag(
                    &gcm, MBEDTLS_GCM_ENCRYPT, plaintext.size(),
                    iv.data(), iv.size(), nullptr, 0,
                    plaintext.data(), ciphertext.data() + ciphertextOffset,
                    TAG_SIZE, tag.data()) != 0) {
                SPDLOG_ERROR("Failed to encrypt data");
                mbedtls_gcm_free(&gcm);
                mbedtls_entropy_free(&entropy);
                mbedtls_ctr_drbg_free(&ctr_drbg);
                return false;
            }

            // Add the tag to the ciphertext
            ciphertext.insert(ciphertext.end(), tag.begin(), tag.end());

            // Clean up
            mbedtls_gcm_free(&gcm);
            mbedtls_entropy_free(&entropy);
            mbedtls_ctr_drbg_free(&ctr_drbg);

            SPDLOG_DEBUG("Data encrypted successfully: {} bytes -> {} bytes",
                         plaintext.size(), ciphertext.size());

            return true;

        } catch (const std::exception& e) {
            SPDLOG_ERROR("Encryption error: {}", e.what());
            return false;
        }
    }

    bool Encryption::decrypt(const std::vector<uint8_t>& ciphertext,
                             const std::string& password,
                             std::vector<uint8_t>& plaintext) {
        if (!s_initialized) {
            init();
        }

        try {
            // Check if ciphertext is large enough to contain salt + IV + tag
            if (ciphertext.size() < 8 + IV_SIZE + TAG_SIZE) {
                SPDLOG_ERROR("Ciphertext is too short");
                return false;
            }

            // Extract salt, IV, and tag from ciphertext
            std::vector<uint8_t> salt(ciphertext.begin(), ciphertext.begin() + 8);
            std::vector<uint8_t> iv(ciphertext.begin() + 8, ciphertext.begin() + 8 + IV_SIZE);

            // Calculate offsets and sizes
            size_t ciphertextOffset = 8 + IV_SIZE;
            size_t ciphertextSize = ciphertext.size() - ciphertextOffset - TAG_SIZE;
            std::vector<uint8_t> tag(ciphertext.end() - TAG_SIZE, ciphertext.end());

            // Derive key from password and salt
            std::vector<uint8_t> key(KEY_SIZE);
            std::vector<uint8_t> derivedIV(IV_SIZE);
            if (!deriveKeyAndIV(password, salt, key, derivedIV)) {
                SPDLOG_ERROR("Failed to derive key and IV");
                return false;
            }

            // Initialize GCM context
            mbedtls_gcm_context gcm;
            mbedtls_gcm_init(&gcm);

            if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key.data(), key.size() * 8) != 0) {
                SPDLOG_ERROR("Failed to set GCM key");
                mbedtls_gcm_free(&gcm);
                return false;
            }

            // Prepare output buffer
            plaintext.clear();
            plaintext.resize(ciphertextSize);

            // Decrypt
            if (mbedtls_gcm_auth_decrypt(
                    &gcm, ciphertextSize,
                    iv.data(), iv.size(), nullptr, 0,
                    tag.data(), TAG_SIZE,
                    ciphertext.data() + ciphertextOffset, plaintext.data()) != 0) {
                SPDLOG_ERROR("Decryption failed: authentication failed or corrupted data");
                mbedtls_gcm_free(&gcm);
                return false;
            }

            // Clean up
            mbedtls_gcm_free(&gcm);

            SPDLOG_DEBUG("Data decrypted successfully: {} bytes -> {} bytes",
                         ciphertext.size(), plaintext.size());

            return true;

        } catch (const std::exception& e) {
            SPDLOG_ERROR("Decryption error: {}", e.what());
            return false;
        }
    }

    std::string Encryption::calculateFileHash(const std::string& filePath) {
        try {
            // Open the file
            std::ifstream file(filePath, std::ios::binary);
            if (!file) {
                throw std::runtime_error("Failed to open file: " + filePath);
            }

            // Initialize the MD context
            mbedtls_md_context_t md_ctx;
            mbedtls_md_init(&md_ctx);

            const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
            if (md_info == nullptr) {
                mbedtls_md_free(&md_ctx);
                throw std::runtime_error("Failed to get SHA-256 info");
            }

            if (mbedtls_md_setup(&md_ctx, md_info, 0) != 0) {
                mbedtls_md_free(&md_ctx);
                throw std::runtime_error("Failed to set up MD context");
            }

            if (mbedtls_md_starts(&md_ctx) != 0) {
                mbedtls_md_free(&md_ctx);
                throw std::runtime_error("Failed to start MD context");
            }

            // Read and hash the file in chunks
            const size_t BUFFER_SIZE = 8192;
            std::vector<unsigned char> buffer(BUFFER_SIZE);

            while (file) {
                file.read(reinterpret_cast<char*>(buffer.data()), BUFFER_SIZE);
                std::streamsize bytesRead = file.gcount();

                if (bytesRead > 0) {
                    if (mbedtls_md_update(&md_ctx, buffer.data(), static_cast<size_t>(bytesRead)) != 0) {
                        mbedtls_md_free(&md_ctx);
                        throw std::runtime_error("Failed to update hash");
                    }
                }
            }

            // Finalize the hash
            std::vector<unsigned char> hash(32); // SHA-256 produces 32 bytes

            if (mbedtls_md_finish(&md_ctx, hash.data()) != 0) {
                mbedtls_md_free(&md_ctx);
                throw std::runtime_error("Failed to finalize hash");
            }

            // Clean up
            mbedtls_md_free(&md_ctx);

            // Convert hash to hex string
            std::stringstream ss;
            ss << std::hex << std::setfill('0');

            for (unsigned char i : hash) {
                ss << std::setw(2) << static_cast<int>(i);
            }

            return ss.str();

        } catch (const std::exception& e) {
            SPDLOG_ERROR("Hash calculation error: {}", e.what());
            return "";
        }
    }

    bool Encryption::verifyFileHash(const std::string& filePath, const std::string& expectedHash) {
        std::string calculatedHash = calculateFileHash(filePath);

        if (calculatedHash.empty()) {
            SPDLOG_ERROR("Failed to calculate file hash for verification");
            return false;
        }

        bool match = (calculatedHash == expectedHash);

        if (!match) {
            SPDLOG_ERROR("File hash mismatch: expected {}, got {}", expectedHash, calculatedHash);
        } else {
            SPDLOG_DEBUG("File hash verified successfully");
        }

        return match;
    }

    bool Encryption::deriveKeyAndIV(const std::string& password,
                                    const std::vector<uint8_t>& salt,
                                    std::vector<uint8_t>& key,
                                    std::vector<uint8_t>& iv) {
        // Initialize MD context for HMAC
        mbedtls_md_context_t ctx;
        mbedtls_md_init(&ctx);

        // Set up the context to use SHA-256
        const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        if (md_info == nullptr) {
            SPDLOG_ERROR("Failed to get SHA-256 info");
            return false;
        }

        if (mbedtls_md_setup(&ctx, md_info, 1) != 0) { // 1 indicates HMAC mode
            SPDLOG_ERROR("Failed to set up MD context");
            mbedtls_md_free(&ctx);
            return false;
        }

        // Allocate a buffer for both key and IV
        std::vector<unsigned char> output(KEY_SIZE + IV_SIZE);

        // Call PBKDF2-HMAC function with the correct parameters
        int ret = mbedtls_pkcs5_pbkdf2_hmac(
                &ctx,
                reinterpret_cast<const unsigned char*>(password.c_str()), password.length(),
                salt.data(), salt.size(),
                10000, // Iteration count
                KEY_SIZE + IV_SIZE,
                output.data()
        );

        // Clean up
        mbedtls_md_free(&ctx);

        if (ret != 0) {
            SPDLOG_ERROR("PBKDF2 key derivation failed: error code {}", ret);
            return false;
        }

        // Copy to key and IV
        std::copy_n(output.begin(), KEY_SIZE, key.begin());
        std::copy_n(output.begin() + KEY_SIZE, IV_SIZE, iv.begin());

        return true;
    }
}