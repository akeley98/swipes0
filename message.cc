#include <array>
#include <byteswap.h>
#include <stddef.h>
#include <stdexcept>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <tuple>

struct MessageDataTypeBase
{
    uint64_t value;
};

struct WrongMagicNumber { };

#define DEF_MESSAGE_DATA_TYPE(TYPENAME) \
struct TYPENAME : MessageDataTypeBase { \
    explicit TYPENAME(uint64_t ARG = 0) { value = ARG; } \
    uint64_t* ptr() { return &value; } \
    operator uint64_t() const { return value; } \
    void swap_endian() { value = bswap_64(value); } \
};

DEF_MESSAGE_DATA_TYPE(SenderID);

using Password = std::array<char, 64>;

struct SerialMessageHeader
{
    uint64_t sender_id;
    Password password;
    uint64_t magic_number;
};

void validate_password(const SerialMessageHeader&)
{

}

struct SerialMessage
{
    SerialMessageHeader header;
    uint64_t payload[];
};

template <uint32_t MagicNumber, typename...DataTypes>
class Message
{
    static_assert(
        MagicNumber != 0,
        "Need nonzero magic number (for endian check)");

    // Tuple storing the uint64_t parameters of this message. It's
    // important that the LAST one is the sender id of the original
    // serial message (it's a bit slimy but that assumption is hard
    // coded elsewhere).
    std::tuple<DataTypes..., SenderID> tup;
  public:
    std::string text;

    // Convert from a serial message (stored in the given string's
    // bytes) to this message type. Performs password validation and
    // fixes endianness if the input had the wrong endianness
    // (detected through the magic number) Throws
    //
    // WrongMagicNumber: the magic number is wrong.
    //
    // std::out_of_range: the magic number was correct, but serial
    // message is too short to hold the expected message.
    //
    // std::invalid_argument: password validation failed.
    explicit Message(const std::string& message_bytes)
    {
        // Extract the serial message header.
        SerialMessageHeader header;
        if (message_bytes.size() < sizeof header) {
            throw std::out_of_range("Serial message too short for header");
        }
        const SerialMessage& serial_message =
            *reinterpret_cast<const SerialMessage*>(message_bytes.data());
        memcpy(&header, &serial_message.header, sizeof header);

        // Validate the sender user id & password in the header.
        validate_password(header);

        // Check if the magic number matches the one assigned for the
        // message type being constructed. We also detect possible
        // endian swaps here (Note that MagicNumber is a nonzero 32
        // bit value while the magic number field is 64 bit -- this
        // guarantees the high 4 bytes are all zero and the low 4 are
        // not all zero, so it's impossible for the magic number field
        // to be a valid magic number both forwards and backwards).
        bool should_swap_endian = false;
        if (MagicNumber != header.magic_number) {
            if (MagicNumber == bswap_64(header.magic_number)) {
                should_swap_endian = true;
            }
            else {
                throw WrongMagicNumber();
            }
        }

        // Check that the message is long enough.
        constexpr size_t uint64_t_count = std::tuple_size<decltype(tup)>::value;
        size_t minimum_size = uint64_t_count * sizeof(uint64_t)
                            + sizeof serial_message;

        if (message_bytes.size() < minimum_size) {
            throw std::out_of_range(
                "Serial message too short for message with magic number "
                + std::to_string(MagicNumber));
        }

        // Copy the array of 64 bit values from the serial message to the
        // message's tuple, possibly fixing endian issues along the way.
        initialize_tuple<uint64_t_count - 1>(
            should_swap_endian,
            serial_message);

        // Remainder of the message is treated as text (no endian swaps).
        text = std::string(
            &message_bytes[minimum_size],
            &message_bytes[message_bytes.size()]);
    }

  private:
    template <size_t Index>
    void initialize_tuple(
        bool should_swap_endian,
        const SerialMessage& serial_message)
    {
        uint64_t& value_ref = std::get<Index>(tup).value;
        uint64_t n;
        // Last value in the tuple is the sender id; others are copied
        // from the payload array.
        if constexpr (Index == std::tuple_size<decltype(tup)>::value - 1) {
            n = serial_message.header.sender_id;
        }
        else {
            n = serial_message.payload[Index];
        }
        value_ref = should_swap_endian ? bswap_64(n) : n;
        if constexpr (Index != 0) {
            initialize_tuple<Index-1>(should_swap_endian, serial_message);
        }
    }

  public:
    template <typename T>
    T& get()
    {
        return std::get<T>(tup);
    }

    template <typename T>
    const T& get() const
    {
        return std::get<T>(tup);
    }

    template <typename T>
    explicit operator T() const {
        return std::get<T>(tup);
    }
};

DEF_MESSAGE_DATA_TYPE(PriceCents)
DEF_MESSAGE_DATA_TYPE(DiningHallBitfield)

using BuyQuery = Message<88, PriceCents, DiningHallBitfield>;

int main()
{
    std::string message;
    message.push_back(100);
    for (int i = 0; i < 71; ++i) message.push_back(0);
    message.push_back(88);
    for (int i = 0; i < 7; ++i) message.push_back(0);

    message.push_back(1);
    for (int i = 0; i < 7; ++i) message.push_back(0);

    message.push_back(3);
    for (int i = 0; i < 7; ++i) message.push_back(0);

    message += "Hello, text!\n";

    BuyQuery bq(message);

    printf("PriceCents: %lu\n", uint64_t(PriceCents(bq)));
    printf("DiningHallBitfield: %lu\n", uint64_t(DiningHallBitfield(bq)));
    printf("SenderID: %lu\n", uint64_t(SenderID(bq)));
}
