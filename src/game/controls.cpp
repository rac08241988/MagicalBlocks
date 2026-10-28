#include "SDL.h"
#include <array>

#include "librecomp/helpers.hpp"
#include "recomp_input.h"
#include "ultramodern/ultramodern.hpp"

// Arrays that hold the mappings for every input for keyboard and controller respectively.
using input_mapping = std::array<recomp::InputField, recomp::bindings_per_input>;
using input_mapping_array = std::array<input_mapping, static_cast<size_t>(recomp::GameInput::COUNT)>;
static input_mapping_array keyboard_input_mappings{};
static input_mapping_array controller_input_mappings{};

// Make the button value array, which maps a button index to its bit field.
#define DEFINE_INPUT(name, value, readable) uint16_t(value##u),
static const std::array n64_button_values = {
    DEFINE_N64_BUTTON_INPUTS()
};
#undef DEFINE_INPUT

// Make the input name array.
#define DEFINE_INPUT(name, value, readable) readable,
static const std::vector<std::string> input_names = {
    DEFINE_ALL_INPUTS()
};
#undef DEFINE_INPUT

// Make the input enum name array.
#define DEFINE_INPUT(name, value, readable) #name,
static const std::vector<std::string> input_enum_names = {
    DEFINE_ALL_INPUTS()
};
#undef DEFINE_INPUT

size_t recomp::get_num_inputs() {
    return (size_t)GameInput::COUNT;
}

const std::string& recomp::get_input_name(GameInput input) {
    return input_names.at(static_cast<size_t>(input));
}

const std::string& recomp::get_input_enum_name(GameInput input) {
    return input_enum_names.at(static_cast<size_t>(input));
}

recomp::GameInput recomp::get_input_from_enum_name(const std::string_view enum_name) {
    auto find_it = std::find(input_enum_names.begin(), input_enum_names.end(), enum_name);
    if (find_it == input_enum_names.end()) {
        return recomp::GameInput::COUNT;
    }

    return static_cast<recomp::GameInput>(find_it - input_enum_names.begin());
}

// Due to an RmlUi limitation this can't be const. Ideally it would return a const reference or even just a straight up copy.
recomp::InputField& recomp::get_input_binding(GameInput input, size_t binding_index, recomp::InputDevice device) {
    input_mapping_array& device_mappings = (device == recomp::InputDevice::Controller) ?  controller_input_mappings : keyboard_input_mappings;
    input_mapping& cur_input_mapping = device_mappings.at(static_cast<size_t>(input));

    if (binding_index < cur_input_mapping.size()) {
        return cur_input_mapping[binding_index];
    }
    else {
        static recomp::InputField dummy_field = {};
        return dummy_field;
    }
}

void recomp::set_input_binding(recomp::GameInput input, size_t binding_index, recomp::InputDevice device, recomp::InputField value) {
    input_mapping_array& device_mappings = (device == recomp::InputDevice::Controller) ?  controller_input_mappings : keyboard_input_mappings;
    input_mapping& cur_input_mapping = device_mappings.at(static_cast<size_t>(input));

    if (binding_index < cur_input_mapping.size()) {
        cur_input_mapping[binding_index] = value;
    }
}

// Player 2 keyboard layout (fixed, so a second player can join VS mode on one keyboard):
// numpad 8/4/2/6 = D-Pad, numpad 0 = A, numpad . = B, numpad Enter = Start, numpad + = Z,
// numpad 7 / 9 = L / R, numpad / * - 5 = C Left / C Right / C Up / C Down.
static const std::array<std::pair<SDL_Scancode, uint16_t>, 14> p2_keyboard = {{
    { SDL_SCANCODE_KP_8, 0x0800 }, { SDL_SCANCODE_KP_2, 0x0400 }, { SDL_SCANCODE_KP_4, 0x0200 }, { SDL_SCANCODE_KP_6, 0x0100 },
    { SDL_SCANCODE_KP_0, 0x8000 }, { SDL_SCANCODE_KP_PERIOD, 0x4000 }, { SDL_SCANCODE_KP_ENTER, 0x1000 },
    { SDL_SCANCODE_KP_PLUS, 0x2000 }, { SDL_SCANCODE_KP_7, 0x0020 }, { SDL_SCANCODE_KP_9, 0x0010 },
    { SDL_SCANCODE_KP_DIVIDE, 0x0002 }, { SDL_SCANCODE_KP_MULTIPLY, 0x0001 }, { SDL_SCANCODE_KP_MINUS, 0x0008 },
    { SDL_SCANCODE_KP_5, 0x0004 },
}};

bool recomp::get_n64_input(int controller_num, uint16_t* buttons_out, float* x_out, float* y_out) {
    uint16_t cur_buttons = 0;
    float cur_x = 0.0f;
    float cur_y = 0.0f;
    
    // Player 1: keyboard bindings + gamepad 1. Player 2: numpad + gamepad 2. Players 3/4: gamepads 3/4.
    if (controller_num < 0 || controller_num > 3) {
        return false;
    }
    if (controller_num >= 2 && controller_num >= recomp::get_gamepad_count()) {
        return false;
    }

    if (!recomp::game_input_disabled()) {
        recomp::set_gamepad_filter(controller_num);
        for (size_t i = 0; i < n64_button_values.size(); i++) {
            size_t input_index = (size_t)GameInput::N64_BUTTON_START + i;
            if (controller_num == 0) {
                cur_buttons |= recomp::get_input_digital(keyboard_input_mappings[input_index]) ? n64_button_values[i] : 0;
            }
            cur_buttons |= recomp::get_input_digital(controller_input_mappings[input_index]) ? n64_button_values[i] : 0;
        }
        if (controller_num == 1) {
            int numkeys = 0;
            const Uint8* keys = SDL_GetKeyboardState(&numkeys);
            for (const auto& [code, bit] : p2_keyboard) {
                if (keys != nullptr && code < numkeys && keys[code]) cur_buttons |= bit;
            }
        }

        float joystick_deadzone = recomp::get_joystick_deadzone() / 100.0f;

        float joystick_x = recomp::get_input_analog(controller_input_mappings[(size_t)GameInput::X_AXIS_POS])
                        - recomp::get_input_analog(controller_input_mappings[(size_t)GameInput::X_AXIS_NEG]);

        float joystick_y = recomp::get_input_analog(controller_input_mappings[(size_t)GameInput::Y_AXIS_POS])
                        - recomp::get_input_analog(controller_input_mappings[(size_t)GameInput::Y_AXIS_NEG]);
        recomp::set_gamepad_filter(-1);

        recomp::apply_joystick_deadzone(joystick_x, joystick_y, &joystick_x, &joystick_y);

        cur_x = joystick_x;
        cur_y = joystick_y;
        if (controller_num == 0) {
            cur_x += recomp::get_input_analog(keyboard_input_mappings[(size_t)GameInput::X_AXIS_POS])
                   - recomp::get_input_analog(keyboard_input_mappings[(size_t)GameInput::X_AXIS_NEG]);
            cur_y += recomp::get_input_analog(keyboard_input_mappings[(size_t)GameInput::Y_AXIS_POS])
                   - recomp::get_input_analog(keyboard_input_mappings[(size_t)GameInput::Y_AXIS_NEG]);
        }
    }

    *buttons_out = cur_buttons;
    *x_out = std::clamp(cur_x, -1.0f, 1.0f);
    *y_out = std::clamp(cur_y, -1.0f, 1.0f);

    return true;
}
