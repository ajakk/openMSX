#include "BooleanInput.hh"

#include "Event.hh"
#include "SDLKey.hh"

#include "one_of.hh"
#include "stl.hh"
#include "strCat.hh"
#include "StringOp.hh"
#include "unreachable.hh"

#include <tuple>

namespace openmsx {

std::string toString(const BooleanInput& input)
{
	return std::visit(overloaded{
		[](const BooleanKeyboard& k) {
			return strCat("keyb ", SDLKey::toString(k.getKeyCode()));
		},
		[](const BooleanMouseButton& m) {
			return strCat("mouse button", m.getButton());
		},
		[](const BooleanJoystickButton& j) {
			return strCat("joy", (j.getJoystick() + 1), " button", j.getButton());
		},
		[](const BooleanJoystickHat& h) {
			const char* str = [&] {
				switch (h.getValue()) {
					case BooleanJoystickHat::UP:    return " up";
					case BooleanJoystickHat::RIGHT: return " right";
					case BooleanJoystickHat::DOWN:  return " down";
					case BooleanJoystickHat::LEFT:  return " left";
					default: UNREACHABLE; return "";
				}
			}();
			return strCat("joy", (h.getJoystick() + 1), " hat", h.getHat(), str);
		},
		[&](const BooleanJoystickAxis& a) {
			return strCat("joy", (a.getJoystick() + 1), ' ',
			              (a.getDirection() == BooleanJoystickAxis::POS ? '+' : '-'),
			              "axis", a.getAxis());
		}
	}, input);
}

[[nodiscard]] static std::optional<unsigned> parseValueWithPrefix(std::string_view str, std::string_view prefix)
{
	if (!str.starts_with(prefix)) return std::nullopt;
	str.remove_prefix(prefix.size());
	return StringOp::stringToBase<10, unsigned>(str);
}

std::optional<BooleanInput> parseBooleanInput(std::string_view text)
{
	auto tokenizer = StringOp::split_view<StringOp::REMOVE_EMPTY_PARTS>(text, ' ');
	auto it = tokenizer.begin();
	auto et = tokenizer.end();

	if (it == et) return std::nullopt;
	auto type = *it++;
	if (it == et) return std::nullopt;
	if (type == "keyb") {
		std::string key{*it++};
		while (it != et) strAppend(key, ' ', *it++); // allow for key-name containing space chars
		auto keycode = SDLKey::keycodeFromString(key);
		if (keycode == SDLK_UNKNOWN) return std::nullopt;
		return BooleanKeyboard(keycode);

	} else if (type == "mouse") {
		auto button = *it++;
		if (it != et) return std::nullopt;
		auto n = parseValueWithPrefix(button, "button");
		if (!n) return std::nullopt;
		return BooleanMouseButton(*n);

	} else if (auto joystick_ = parseValueWithPrefix(type, "joy")) {
		if (*joystick_ == 0) return std::nullopt;
		unsigned joystick = *joystick_ - 1;

		auto subType = *it++;
		if (auto button = parseValueWithPrefix(subType, "button")) {
			if (it != et) return std::nullopt;
			return BooleanJoystickButton(joystick, *button);

		} else if (auto hat = parseValueWithPrefix(subType, "hat")) {
			if (it == et) return std::nullopt;
			auto valueStr = *it++;
			if (it != et) return std::nullopt;

			BooleanJoystickHat::Value value;
			if      (valueStr == "up"   ) value = BooleanJoystickHat::UP;
			else if (valueStr == "right") value = BooleanJoystickHat::RIGHT;
			else if (valueStr == "down" ) value = BooleanJoystickHat::DOWN;
			else if (valueStr == "left" ) value = BooleanJoystickHat::LEFT;
			else return std::nullopt;

			return BooleanJoystickHat(joystick, *hat, value);

		} else if (auto pAxis = parseValueWithPrefix(subType, "+axis")) {
			if (it != et) return std::nullopt;
			return BooleanJoystickAxis(joystick, *pAxis, BooleanJoystickAxis::POS);
		} else if (auto nAxis = parseValueWithPrefix(subType, "-axis")) {
			if (it != et) return std::nullopt;
			return BooleanJoystickAxis(joystick, *nAxis, BooleanJoystickAxis::NEG);
		}
	}
	return std::nullopt;
}

std::optional<BooleanInput> captureBooleanInput(const Event& event, std::function<int(int)> getJoyDeadZone)
{
	return std::visit(overloaded{
		[](const KeyDownEvent& e) -> std::optional<BooleanInput> {
			return BooleanKeyboard(e.getKeyCode());
		},
		[](const MouseButtonDownEvent& e) -> std::optional<BooleanInput> {
			return BooleanMouseButton(e.getButton());
		},
		[](const JoystickButtonDownEvent& e) -> std::optional<BooleanInput> {
			return BooleanJoystickButton(e.getJoystick(), e.getButton());
		},
		[](const JoystickHatEvent& e) -> std::optional<BooleanInput> {
			auto value = e.getValue();
			if (value != one_of(SDL_HAT_UP, SDL_HAT_RIGHT, SDL_HAT_DOWN, SDL_HAT_LEFT)) {
				return std::nullopt;
			}
			return BooleanJoystickHat(e.getJoystick(), e.getHat(),
			                          BooleanJoystickHat::Value(value));
		},
		[&](const JoystickAxisMotionEvent& e) -> std::optional<BooleanInput> {
			auto joystick = e.getJoystick();
			int deadZone = getJoyDeadZone(joystick); // percentage 0..100
			int threshold = (deadZone * 32768) / 100;

			auto value = e.getValue();
			if ((-threshold <= value) && (value <= threshold)) {
				return std::nullopt;
			}
			return BooleanJoystickAxis(joystick, e.getAxis(),
			                     (value > 0 ? BooleanJoystickAxis::POS
			                                : BooleanJoystickAxis::NEG));
		},
		[](const EventBase&) -> std::optional<BooleanInput> {
			return std::nullopt;
		}
	}, event);
}

bool operator==(const BooleanInput& x, const BooleanInput& y)
{
	return std::visit(overloaded{
		[](const BooleanKeyboard& k1, const BooleanKeyboard& k2) {
			return k1.getKeyCode() == k2.getKeyCode();
		},
		[](const BooleanMouseButton& m1, const BooleanMouseButton& m2) {
			return m1.getButton() == m2.getButton();
		},
		[](const BooleanJoystickButton& j1, const BooleanJoystickButton& j2) {
			return std::tuple(j1.getJoystick(), j1.getButton()) ==
			       std::tuple(j2.getJoystick(), j2.getButton());
		},
		[](const BooleanJoystickHat& j1, const BooleanJoystickHat& j2) {
			return std::tuple(j1.getJoystick(), j1.getHat(), j1.getValue()) ==
			       std::tuple(j2.getJoystick(), j2.getHat(), j2.getValue());
		},
		[](const BooleanJoystickAxis& j1, const BooleanJoystickAxis& j2) {
			return std::tuple(j1.getJoystick(), j1.getAxis(), j1.getDirection()) ==
			       std::tuple(j2.getJoystick(), j2.getAxis(), j2.getDirection());
		},
		[](const auto&, const auto&) { // mixed types
			return false;
		}
	}, x, y);
}

std::optional<bool> match(const BooleanInput& binding, const Event& event,
                          std::function<int(int)> getJoyDeadZone)
{
	return std::visit(overloaded{
		[](const BooleanKeyboard& bind, const KeyDownEvent& down) -> std::optional<bool> {
			if (bind.getKeyCode() == down.getKeyCode()) return true;
			return std::nullopt;
		},
		[](const BooleanKeyboard& bind, const KeyUpEvent& up) -> std::optional<bool> {
			if (bind.getKeyCode() == up.getKeyCode()) return false; // no longer pressed
			return std::nullopt;
		},

		[](const BooleanMouseButton& bind, const MouseButtonDownEvent& down) -> std::optional<bool> {
			if (bind.getButton() == down.getButton()) return true;
			return std::nullopt;
		},
		[](const BooleanMouseButton& bind, const MouseButtonUpEvent& up) -> std::optional<bool> {
			if (bind.getButton() == up.getButton()) return false; // no longer pressed
			return std::nullopt;
		},

		[](const BooleanJoystickButton& bind, const JoystickButtonDownEvent& down) -> std::optional<bool> {
			if (bind.getJoystick() != down.getJoystick()) return std::nullopt;
			if (bind.getButton() == down.getButton()) return true;
			return std::nullopt;
		},
		[](const BooleanJoystickButton& bind, const JoystickButtonUpEvent& up) -> std::optional<bool> {
			if (bind.getJoystick() != up.getJoystick()) return std::nullopt;
			if (bind.getButton() == up.getButton()) return false; // no longer pressed
			return std::nullopt;
		},

		[](const BooleanJoystickHat& bind, const JoystickHatEvent& e) -> std::optional<bool> {
			if (bind.getJoystick() != e.getJoystick()) return std::nullopt;
			if (bind.getHat() != e.getHat()) return std::nullopt;
			return bind.getValue() & e.getValue();
		},

		[&](const BooleanJoystickAxis& bind, const JoystickAxisMotionEvent& e) -> std::optional<bool> {
			if (bind.getJoystick() != e.getJoystick()) return std::nullopt;
			if (bind.getAxis() != e.getAxis()) return std::nullopt;
			int deadZone = getJoyDeadZone(bind.getJoystick()); // percentage 0..100
			int threshold = (deadZone * 32768) / 100;
			if (bind.getDirection() == BooleanJoystickAxis::POS) {
				return e.getValue() > threshold;
			} else {
				return e.getValue() < -threshold;
			}
		},

		[](const auto& /*bind*/, const auto& /*event*/) -> std::optional<bool> {
			return std::nullopt;
		}
	}, binding, event);
}

} // namespace openmsx
