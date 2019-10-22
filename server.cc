// Adapted from websocketpp echo_server example
#include <websocketpp/config/asio_no_tls.hpp>

#include <websocketpp/server.hpp>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <stdint.h>
#include <string>
#include <utility>

// Bid prices will be represented in cents with 16-bit unsigned integers.
using cents = uint16_t;

// Meaning of the flag field of a bid.
constexpr uint8_t selling_flag = 1;
constexpr uint8_t bplate_flag = 2;
constexpr uint8_t covel_flag = 4;
constexpr uint8_t deneve_flag = 8;
constexpr uint8_t feast_flag = 16;

// Used to internally store bids created by users.
struct bid_info
{
    // Cents the user is willing to pay/accept in order to buy/sell a swipe.
    cents bid_cents = 0;
    
    // Flags representing the dining halls the user is interested in, and
    // whether the user is buying or selling.
    uint8_t flags = 0;
    
    // Username of the user.
    std::string username;
    
    // Explicitly declare special constructors/member functions.
    bid_info() = default;
    ~bid_info() = default;
    bid_info(bid_info&&) = default;
    bid_info(const bid_info&) = default;
    bid_info& operator=(bid_info&&) = default;
    bid_info& operator=(const bid_info&) = default;
    
    // Dumb constructor.
    bid_info(cents cents_, uint8_t flags_, std::string username_) :
        bid_cents(cents_),
        flags(flags_),
        username(std::move(username_)) {}
    
    // Convert bytes from a websocket message into a bid_info.
    //
    // Byte 0: Magic number (130)
    // Byte 1: Flags
    // Byte 2: Cents low (cents % 256)
    // Byte 3: Cents high (cents / 256)
    // Remaining bytes: username
    static bid_info from_message(std::string const& msg)
    {
        if (msg.size() < 4) {
            // Obviously should catch this for the real program.
            throw std::runtime_error("Expected 4+ bytes from bid message");
        }
        
        if (uint8_t(msg[0]) != 130) {
            throw std::runtime_error("Expected magic number 130");
        }
        
        uint8_t flags = msg[1];
        cents bid_cents = msg[3];
        bid_cents <<= 8;
        bid_cents |= uint8_t(msg[2]);
        
        std::string username(&msg[4], &msg[msg.size()]);
        
        return bid_info(bid_cents, flags, std::move(username));
    }
    
    // Return true iff we should match the two bids (opposite
    // buying/selling, overlapping dining hall options, and price is
    // right). If so, also append a text representation of the other
    // bid to the *out string.
    bool try_append_match_string(const bid_info& other, std::string* out)
    {
        const bid_info* buyer = nullptr;
        const bid_info* seller = nullptr;
        
        if (flags & selling_flag) {
            seller = this;
        } else {
            buyer = this;
        }
        
        if (other.flags & selling_flag) {
            seller = &other;
        } else {
            buyer = &other;
        }
        
        // No match if both are selling/buying.
        if (buyer == nullptr || seller == nullptr) {
            return false;
        }
        
        // Check for dining hall overlaps, and fail if there are none.
        auto anded_flags = flags & other.flags;
        auto bplate = anded_flags & bplate_flag;
        auto covel = anded_flags & covel_flag;
        auto deneve = anded_flags & deneve_flag;
        auto feast = anded_flags & feast_flag;
        if (!bplate && !covel && !deneve && !feast) {
            return false;
        }
        
        // Fail if the seller wants more than the buyer is willing to pay.
        if (seller->bid_cents > buyer->bid_cents) {
            return false;
        }
        
        // At this point, the two users' bids should be matched. Write
        // a string representing the other bid. However, so that the
        // seller and buyer both see the same price, we always report
        // the price the seller offered.
        std::string result;
        auto dollars = seller->bid_cents / 100;
        auto cents_tens = (seller->bid_cents / 10) % 10;
        auto cents_ones = seller->bid_cents % 10;
        result += "$";
        result += std::to_string(dollars);
        result += ".";
        result += std::to_string(cents_tens);
        result += std::to_string(cents_ones);
        if (bplate) result += " | bplate";
        if (covel) result += " | covel";
        if (deneve) result += " | deneve";
        if (feast) result += " | feast";
        result += " | user: '";
        result += other.username;
        result += "'\n";
        
        *out += std::move(result);
        return true;
    }
    
    // Print debug view of the bid_info.
    friend std::ostream& operator<< (std::ostream& out, const bid_info& bid)
    {
        out << "bid_info(" << bid.bid_cents << ", "
            << unsigned(bid.flags) << ", \""
            << bid.username << "\")";
        return out;
    }
};

// Write a pointer to the global vector of user bids to *out, and
// return a (unique pointer to) lock_guard for the mutex that
// synchronizes access to the said vector of bids.
[[nodiscard]] std::unique_ptr<std::lock_guard<std::mutex>>
lock_and_view_bids(std::vector<bid_info>** out)
{
    static std::vector<bid_info> the_bids;
    static std::mutex the_mutex;
    
    *out = &the_bids;
    return std::make_unique<std::lock_guard<std::mutex>>(the_mutex);
}

typedef websocketpp::server<websocketpp::config::asio> server;

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

// pull out the type of messages sent by our config
typedef server::message_ptr message_ptr;

// Define a callback to handle incoming messages. Here, we interpret
// the message as a bid, and either create or update a bid in the global
// list of bids, depending on whether the username is new. Then, we send
// a list of matching bids back to the user.
void on_message(server* s, websocketpp::connection_hdl hdl, message_ptr msg) {
    std::string input = msg->get_payload();
    std::string output;
    
    std::cout << "input: \"" << input << "\"\n";
    std::cout.flush();
    
    // This block is synchronized by the bid vector's lock.
    {
        std::vector<bid_info>* bids;
        auto bids_lock_guard = lock_and_view_bids(&bids);
        
        // Parse the message received into a bid.
        auto new_bid = bid_info::from_message(input);
        
        // Find the location to insert/modify a bid.
        auto begin_it = begin(*bids);
        auto end_it = end(*bids);
        auto same_username = [&new_bid] (const bid_info& other) {
            return other.username == new_bid.username;
        };
        auto insert_it = std::find_if(begin_it, end_it, same_username);
        
        // Create a new bid if the username is not found; replace existing
        // bid from the same user otherwise.
        if (insert_it == end_it) {
            bids->push_back(new_bid);
            // Guard against iterator invalidation.
            begin_it = begin(*bids);
            end_it = end(*bids);
        } else {
            *insert_it = new_bid;
        }
        
        // Now create a list of matches.
        bool had_match = false;
        for (auto it = begin_it; it != end_it; ++it) {
            std::cout << *it << "\n";
            bool matches = new_bid.try_append_match_string(*it, &output);
            had_match |= matches;
        }
        
        if (!had_match) {
            output = "No matches found";
        }
    }
    
    std::cout << "will output: \n" << output << "\n";
    
    try {
        s->send(hdl, output, msg->get_opcode());
    } catch (websocketpp::exception const & e) {
        std::cout << "Echo failed because: "
                  << "(" << e.what() << ")" << std::endl;
    }
}

int main() {
    // Create a server endpoint
    server echo_server;
    
    server::message_handler f;

    try {
        // Set logging settings
        echo_server.set_access_channels(websocketpp::log::alevel::all);
        echo_server.clear_access_channels(websocketpp::log::alevel::frame_payload);

        // Initialize Asio
        echo_server.init_asio();

        // Register our message handler
        echo_server.set_message_handler(bind(&on_message,&echo_server,::_1,::_2));

        // Listen on port 9002
        echo_server.listen(9002);

        // Start the server accept loop
        echo_server.start_accept();

        // Start the ASIO io_service run loop
        echo_server.run();
    } catch (websocketpp::exception const & e) {
        std::cout << e.what() << std::endl;
    } catch (...) {
        std::cout << "other exception" << std::endl;
    }
}
