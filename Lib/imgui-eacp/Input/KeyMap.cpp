#include "KeyMap.h"

namespace eacp::Gui
{
ImGuiKey toImGuiKey(std::uint16_t keyCode)
{
    using namespace Graphics::KeyCode;

    switch (keyCode)
    {
        case A:
            return ImGuiKey_A;
        case B:
            return ImGuiKey_B;
        case C:
            return ImGuiKey_C;
        case D:
            return ImGuiKey_D;
        case E:
            return ImGuiKey_E;
        case F:
            return ImGuiKey_F;
        case G:
            return ImGuiKey_G;
        case H:
            return ImGuiKey_H;
        case I:
            return ImGuiKey_I;
        case J:
            return ImGuiKey_J;
        case K:
            return ImGuiKey_K;
        case L:
            return ImGuiKey_L;
        case M:
            return ImGuiKey_M;
        case N:
            return ImGuiKey_N;
        case O:
            return ImGuiKey_O;
        case P:
            return ImGuiKey_P;
        case Q:
            return ImGuiKey_Q;
        case R:
            return ImGuiKey_R;
        case S:
            return ImGuiKey_S;
        case T:
            return ImGuiKey_T;
        case U:
            return ImGuiKey_U;
        case V:
            return ImGuiKey_V;
        case W:
            return ImGuiKey_W;
        case X:
            return ImGuiKey_X;
        case Y:
            return ImGuiKey_Y;
        case Z:
            return ImGuiKey_Z;

        case Num0:
            return ImGuiKey_0;
        case Num1:
            return ImGuiKey_1;
        case Num2:
            return ImGuiKey_2;
        case Num3:
            return ImGuiKey_3;
        case Num4:
            return ImGuiKey_4;
        case Num5:
            return ImGuiKey_5;
        case Num6:
            return ImGuiKey_6;
        case Num7:
            return ImGuiKey_7;
        case Num8:
            return ImGuiKey_8;
        case Num9:
            return ImGuiKey_9;

        case Space:
            return ImGuiKey_Space;
        case Return:
            return ImGuiKey_Enter;
        case Tab:
            return ImGuiKey_Tab;
        case Delete:
            return ImGuiKey_Backspace;
        case ForwardDelete:
            return ImGuiKey_Delete;
        case Escape:
            return ImGuiKey_Escape;
        case CapsLock:
            return ImGuiKey_CapsLock;

        case LeftArrow:
            return ImGuiKey_LeftArrow;
        case RightArrow:
            return ImGuiKey_RightArrow;
        case UpArrow:
            return ImGuiKey_UpArrow;
        case DownArrow:
            return ImGuiKey_DownArrow;
        case Home:
            return ImGuiKey_Home;
        case End:
            return ImGuiKey_End;
        case PageUp:
            return ImGuiKey_PageUp;
        case PageDown:
            return ImGuiKey_PageDown;

        case Minus:
            return ImGuiKey_Minus;
        case Equals:
            return ImGuiKey_Equal;
        case LeftBracket:
            return ImGuiKey_LeftBracket;
        case RightBracket:
            return ImGuiKey_RightBracket;
        case Backslash:
            return ImGuiKey_Backslash;
        case Semicolon:
            return ImGuiKey_Semicolon;
        case Quote:
            return ImGuiKey_Apostrophe;
        case Comma:
            return ImGuiKey_Comma;
        case Period:
            return ImGuiKey_Period;
        case Slash:
            return ImGuiKey_Slash;
        case Grave:
            return ImGuiKey_GraveAccent;

        case F1:
            return ImGuiKey_F1;
        case F2:
            return ImGuiKey_F2;
        case F3:
            return ImGuiKey_F3;
        case F4:
            return ImGuiKey_F4;
        case F5:
            return ImGuiKey_F5;
        case F6:
            return ImGuiKey_F6;
        case F7:
            return ImGuiKey_F7;
        case F8:
            return ImGuiKey_F8;
        case F9:
            return ImGuiKey_F9;
        case F10:
            return ImGuiKey_F10;
        case F11:
            return ImGuiKey_F11;
        case F12:
            return ImGuiKey_F12;

        case Keypad0:
            return ImGuiKey_Keypad0;
        case Keypad1:
            return ImGuiKey_Keypad1;
        case Keypad2:
            return ImGuiKey_Keypad2;
        case Keypad3:
            return ImGuiKey_Keypad3;
        case Keypad4:
            return ImGuiKey_Keypad4;
        case Keypad5:
            return ImGuiKey_Keypad5;
        case Keypad6:
            return ImGuiKey_Keypad6;
        case Keypad7:
            return ImGuiKey_Keypad7;
        case Keypad8:
            return ImGuiKey_Keypad8;
        case Keypad9:
            return ImGuiKey_Keypad9;
        case KeypadDecimal:
            return ImGuiKey_KeypadDecimal;
        case KeypadDivide:
            return ImGuiKey_KeypadDivide;
        case KeypadMultiply:
            return ImGuiKey_KeypadMultiply;
        case KeypadMinus:
            return ImGuiKey_KeypadSubtract;
        case KeypadPlus:
            return ImGuiKey_KeypadAdd;
        case KeypadEnter:
            return ImGuiKey_KeypadEnter;
        case KeypadEquals:
            return ImGuiKey_KeypadEqual;
        case KeypadClear:
            return ImGuiKey_NumLock;

        default:
            return ImGuiKey_None;
    }
}

ImGuiMouseButton toImGuiButton(Graphics::MouseButton button)
{
    switch (button)
    {
        case Graphics::MouseButton::Right:
            return ImGuiMouseButton_Right;
        case Graphics::MouseButton::Middle:
            return ImGuiMouseButton_Middle;
        default:
            return ImGuiMouseButton_Left;
    }
}

Graphics::MouseCursor toMouseCursor(ImGuiMouseCursor cursor)
{
    switch (cursor)
    {
        case ImGuiMouseCursor_TextInput:
            return Graphics::MouseCursor::IBeam;
        case ImGuiMouseCursor_ResizeNS:
            return Graphics::MouseCursor::ResizeUpDown;
        case ImGuiMouseCursor_ResizeEW:
            return Graphics::MouseCursor::ResizeLeftRight;
        case ImGuiMouseCursor_ResizeAll:
            return Graphics::MouseCursor::Crosshair;
        case ImGuiMouseCursor_Hand:
            return Graphics::MouseCursor::PointingHand;
        default:
            return Graphics::MouseCursor::Default;
    }
}

void addModifiers(ImGuiIO& io, const Graphics::ModifierKeys& modifiers)
{
    io.AddKeyEvent(ImGuiMod_Shift, modifiers.shift);
    io.AddKeyEvent(ImGuiMod_Ctrl, modifiers.control);
    io.AddKeyEvent(ImGuiMod_Alt, modifiers.alt);
    io.AddKeyEvent(ImGuiMod_Super, modifiers.command);
}
} // namespace eacp::Gui
