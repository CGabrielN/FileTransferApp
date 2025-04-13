#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace network {

    /**
     * Message types for the file transfer protocol
     */
    enum class MessageType {
        TransferRequest,
        TransferResponse,
        FileData,
        TransferComplete,
        TransferCancel
    };

    /**
     * Base message structure for all protocol messages
     */
    struct Message {
        MessageType type;
        std::string transferId;

        virtual ~Message() = default;

        virtual nlohmann::json toJson() const {
            return {
                    {"type",       static_cast<int>(type)},
                    {"transferId", transferId}
            };
        }

        virtual void fromJson(const nlohmann::json &j) {
            type = static_cast<MessageType>(j["type"].get<int>());
            transferId = j["transferId"].get<std::string>();
        }
    };

    /**
     * Message sent to request a file transfer
     */
    struct TransferRequestMessage : public Message {
        std::string senderId;
        std::string senderName;
        std::string fileName;
        std::uintmax_t fileSize;
        std::string fileHash;

        TransferRequestMessage() {
            type = MessageType::TransferRequest;
        }

        nlohmann::json toJson() const override {
            auto j = Message::toJson();
            j["senderId"] = senderId;
            j["senderName"] = senderName;
            j["fileName"] = fileName;
            j["fileSize"] = fileSize;
            j["fileHash"] = fileHash;
            return j;
        }

        void fromJson(const nlohmann::json &j) override {
            Message::fromJson(j);
            senderId = j["senderId"].get<std::string>();
            senderName = j["senderName"].get<std::string>();
            fileName = j["fileName"].get<std::string>();
            fileSize = j["fileSize"].get<std::uintmax_t>();
            fileHash = j["fileHash"].get<std::string>();
        }
    };


    /**
     * Message sent in response to a transfer request
     */
    struct TransferResponseMessage : public Message {
        bool accepted;
        std::string receiverId;
        std::string receiverName;
        std::string filePath; // Path where the file will be saved (if accepted)

        TransferResponseMessage() {
            type = MessageType::TransferResponse;
        }

        nlohmann::json toJson() const override {
            auto j = Message::toJson();
            j["accepted"] = accepted;
            j["receiverId"] = receiverId;
            j["receiverName"] = receiverName;
            j["filePath"] = filePath;
            return j;
        }

        void fromJson(const nlohmann::json &j) override {
            Message::fromJson(j);
            accepted = j["accepted"].get<bool>();
            receiverId = j["receiverId"].get<std::string>();
            receiverName = j["receiverName"].get<std::string>();
            filePath = j["filePath"].get<std::string>();
        }
    };

    /**
     * Message containing file data chunks
     */
    struct FileDataMessage : public Message {
        uint32_t chunkIndex;
        uint32_t totalChunks;
        std::vector<uint8_t> data;

        FileDataMessage() {
            type = MessageType::FileData;
        }

        nlohmann::json toJson() const override {
            auto j = Message::toJson();
            j["chunkIndex"] = chunkIndex;
            j["totalChunks"] = totalChunks;

            // Convert binary data to base64
            //TODO: Fix this
//             std::string base64Data = nlohmann::json::to_cbor(data);
            std::vector<uint8_t> base64Data = nlohmann::json::to_cbor(data);
            j["data"] = base64Data;

            return j;
        }

        void fromJson(const nlohmann::json &j) override {
            Message::fromJson(j);
            chunkIndex = j["chunkIndex"].get<uint32_t>();
            totalChunks = j["totalChunks"].get<uint32_t>();

            // Convert base64 back to binary
            std::string base64Data = j["data"].get<std::string>();
            auto temp = nlohmann::json::from_cbor(base64Data);
            data = temp.get<std::vector<uint8_t>>();
        }
    };

    /**
    * Message sent when a transfer is complete
    */
    struct TransferCompleteMessage : public Message {
        bool success;
        std::string fileHash; // For verification

        TransferCompleteMessage() {
            type = MessageType::TransferComplete;
        }

        nlohmann::json toJson() const override {
            auto j = Message::toJson();
            j["success"] = success;
            j["fileHash"] = fileHash;
            return j;
        }

        void fromJson(const nlohmann::json &j) override {
            Message::fromJson(j);
            success = j["success"].get<bool>();
            fileHash = j["fileHash"].get<std::string>();
        }
    };

    /**
    * Message sent to cancel a transfer
    */
    struct TransferCancelMessage : public Message {
        std::string reason;

        TransferCancelMessage() {
            type = MessageType::TransferCancel;
        }

        nlohmann::json toJson() const override {
            auto j = Message::toJson();
            j["reason"] = reason;
            return j;
        }

        void fromJson(const nlohmann::json &j) override {
            Message::fromJson(j);
            reason = j["reason"].get<std::string>();
        }
    };


    /**
     * Protocol utility class for serializing and deserializing messages
     */
    class Protocol {
    public:
        /**
         * Serialize a message to binary data
         * @param message The message to serialize
         * @return Binary data
         */
        static std::vector<uint8_t> serialize(const Message &message) {
            // Convert message to JSON
            nlohmann::json j = message.toJson();

            // Convert JSON to string
            std::string jsonStr = j.dump();

            // Convert string to binary
            return {jsonStr.begin(), jsonStr.end()};
        }

        static std::unique_ptr<Message> deserialize(const std::vector<uint8_t> &data) {
            // Convert binary to string
            std::string jsonStr(data.begin(), data.end());

            // Parse JSON
            nlohmann::json j = nlohmann::json::parse(jsonStr);

            // Get message type
            auto type = static_cast<MessageType>(j["type"].get<int>());

            // Create appropriate message object
            std::unique_ptr<Message> message;

            switch (type) {
                case MessageType::TransferRequest:
                    message = std::make_unique<TransferRequestMessage>();
                    break;

                case MessageType::TransferResponse:
                    message = std::make_unique<TransferResponseMessage>();
                    break;

                case MessageType::FileData:
                    message = std::make_unique<FileDataMessage>();
                    break;

                case MessageType::TransferComplete:
                    message = std::make_unique<TransferCompleteMessage>();
                    break;

                case MessageType::TransferCancel:
                    message = std::make_unique<TransferCancelMessage>();
                    break;

                default:
                    throw std::runtime_error("Unknown message type");
            }

            // Fill message from JSON
            message->fromJson(j);

            return message;
        }
    };
}