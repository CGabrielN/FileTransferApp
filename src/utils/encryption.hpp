#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace utils {

    /**
     * Encryption utility class
     * Provides encryption, decryption, and hashing functions
     */
    class Encryption {
    public:
        /**
         * Initialize the encryption system
         */
        static void init();

        /**
         * Shutdown the encryption system
         */
        static void shutdown();

        /**
         * Encrypt data using AES-256-GCM with a password
         * @param plaintext Data to encrypt
         * @param password Password for encryption
         * @param ciphertext Output buffer for encrypted data
         * @return True if encryption was successful, false otherwise
         */
        static bool encrypt(const std::vector<uint8_t> &plaintext,
                            const std::string &password,
                            std::vector<uint8_t> &ciphertext);

        /**
         * Decrypt data encrypted with encrypt()
         * @param ciphertext Encrypted data
         * @param password Password for decryption
         * @param plaintext Output buffer for decrypted data
         * @return True if decryption was successful, false otherwise
         */
        static bool decrypt(const std::vector<uint8_t> &ciphertext,
                            const std::string &password,
                            std::vector<uint8_t> &plaintext);

        /**
         * Calculate SHA-256 hash of a file
         * @param filePath Path to the file
         * @return Hexadecimal hash string or empty string on error
         */
        static std::string calculateFileHash(const std::string &filePath);

        /**
         * Verify that a file's hash matches the expected hash
         * @param filePath Path to the file
         * @param expectedHash Expected hash value
         * @return True if the hash matches, false otherwise
         */
        static bool verifyFileHash(const std::string &filePath, const std::string &expectedHash);

    private:
        /**
         * Derive key and IV from password and salt using PBKDF2
         * @param password Password
         * @param salt Salt value
         * @param key Output buffer for key
         * @param iv Output buffer for IV
         * @return True if derivation was successful, false otherwise
         */
        static bool deriveKeyAndIV(const std::string &password,
                                   const std::vector<uint8_t> &salt,
                                   std::vector<uint8_t> &key,
                                   std::vector<uint8_t> &iv);

        /**
         * Flag indicating whether the encryption system is initialized
         */
        static bool s_initialized;

    };
}