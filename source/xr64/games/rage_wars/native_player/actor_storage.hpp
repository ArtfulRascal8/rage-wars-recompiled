#pragma once

#include <bit>
#include <cstdint>
#include <limits>

namespace rage_wars::player {

using ObjectId = std::uint32_t;
struct WordField { std::uint32_t offset; };
struct FloatField { std::uint32_t offset; };

// The game owns object identity and field meaning. Backends own translation,
// lifetime and bounds. An ID is never a host pointer or an owning allocation.
class WordStorage {
public:
    virtual ~WordStorage() = default;
    [[nodiscard]] virtual std::uint32_t load_word(ObjectId, std::uint32_t offset) const = 0;
    [[nodiscard]] virtual std::uint8_t load_byte(ObjectId object, std::uint32_t offset) const {
        const std::uint32_t address = object + offset;
        const std::uint32_t aligned = address & ~std::uint32_t{3};
        const std::uint32_t shift = (3U - (address & 3U)) * 8U;
        return static_cast<std::uint8_t>(load_word(aligned, 0) >> shift);
    }
    [[nodiscard]] virtual std::uint16_t load_halfword(ObjectId object, std::uint32_t offset) const {
        const std::uint32_t address = object + offset;
        const std::uint32_t aligned = address & ~std::uint32_t{3};
        const std::uint32_t shift = ((address & 2U) == 0 ? 16U : 0U);
        return static_cast<std::uint16_t>(load_word(aligned, 0) >> shift);
    }
    virtual void store_word(ObjectId, std::uint32_t offset, std::uint32_t value) = 0;
    virtual void store_byte(ObjectId object, std::uint32_t offset, std::uint8_t value) {
        const std::uint32_t address = object + offset;
        const std::uint32_t aligned = address & ~std::uint32_t{3};
        const std::uint32_t shift = (3U - (address & 3U)) * 8U;
        const std::uint32_t mask = 0xFFU << shift;
        const std::uint32_t word = load_word(aligned, 0);
        store_word(aligned, 0, (word & ~mask) | (static_cast<std::uint32_t>(value) << shift));
    }
};

class ObjectView {
public:
    ObjectView(WordStorage& storage, ObjectId id) : storage_(&storage), id_(id) {}
    [[nodiscard]] ObjectId id() const { return id_; }
    [[nodiscard]] std::uint32_t word(WordField field) const {
        return storage_->load_word(id_, field.offset);
    }
    void word(WordField field, std::uint32_t value) const {
        storage_->store_word(id_, field.offset, value);
    }
    [[nodiscard]] float scalar(FloatField field) const {
        return std::bit_cast<float>(storage_->load_word(id_, field.offset));
    }
    void scalar(FloatField field, float value) const {
        storage_->store_word(id_, field.offset, std::bit_cast<std::uint32_t>(value));
    }
private:
    WordStorage* storage_;
    ObjectId id_;
};

class RageWarsActorStorage {
public:
    explicit RageWarsActorStorage(WordStorage& storage) : storage_(storage) {}
    // Common-prefix and motion/camera fields share the same live object view.
    // Two observed producer arguments may therefore be the same ID.
    [[nodiscard]] ObjectView actor(ObjectId id) const { return {storage_, id}; }
    [[nodiscard]] ObjectView object(ObjectId id) const { return {storage_, id}; }
private:
    WordStorage& storage_;
};

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

namespace fields {
namespace common {
inline constexpr FloatField x{0x08}, y{0x0C}, z{0x10}, angle_6c{0x6C};
inline constexpr WordField region{0x14};
inline constexpr WordField orientation[4]{{0x5C}, {0x60}, {0x64}, {0x68}};
}
namespace actor {
inline constexpr WordField camera_mode{0x5D0}, camera{0x5DC}, input_flags{0x6AC};
inline constexpr FloatField value_708{0x708}, value_718{0x718}, value_720{0x720};
inline constexpr FloatField value_724{0x724}, value_728{0x728}, value_72c{0x72C};
inline constexpr FloatField value_730{0x730}, value_734{0x734}, value_738{0x738};
inline constexpr FloatField value_73c{0x73C}, value_740{0x740}, value_744{0x744};
inline constexpr FloatField value_780{0x780}, blend_7ec{0x7EC};
}
namespace camera {
inline constexpr WordField view_type{0x24}, rotation_28{0x28}, rotation_2c{0x2C};
inline constexpr FloatField parameter_30{0x30}, parameter_34{0x34};
inline constexpr FloatField x{0x38}, y{0x3C}, z{0x40}, eye{0x44};
inline constexpr WordField orientation[4]{{0x48}, {0x4C}, {0x50}, {0x54}};
inline constexpr WordField region{0x58}, visibility{0x64};
inline constexpr FloatField blend{0x5C}, trailing_60{0x60};
}
namespace region { inline constexpr WordField visibility{0x1C}; }
}

} // namespace rage_wars::player
