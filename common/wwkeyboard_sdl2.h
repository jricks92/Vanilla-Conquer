#pragma once
#include "wwkeyboard.h"

class WWKeyboardClassSDL2 : public WWKeyboardClass
{
public:
    virtual ~WWKeyboardClassSDL2();

    virtual void Fill_Buffer_From_System(void);
    virtual bool Is_Gamepad_Active();
    virtual void Open_Controller();
    virtual void Close_Controller();
    virtual bool Is_Analog_Scroll_Active();
    virtual unsigned char Get_Scroll_Direction();
    virtual KeyASCIIType To_ASCII(unsigned short key);

private:
    void Handle_Controller_Axis_Event(const SDL_ControllerAxisEvent& motion);
    void Handle_Controller_Button_Event(const SDL_ControllerButtonEvent& button);
    void Process_Controller_Axis_Motion();
    void Handle_Touch_Event(const SDL_Event& event);
    void Process_Touch();

    // used to convert user-friendly pointer speed values into more useable ones
    static constexpr float CONTROLLER_SPEED_MOD = 2000000.0f;
    // bigger value correndsponds to faster pointer movement speed with bigger stick axis values
    static constexpr float CONTROLLER_AXIS_SPEEDUP = 1.03f;
    // speedup value while the trigger is pressed
    static constexpr int CONTROLLER_TRIGGER_SPEEDUP = 2;

    enum
    {
        CONTROLLER_L_DEADZONE = 4000,
        CONTROLLER_R_DEADZONE = 6000,
        CONTROLLER_TRIGGER_R_DEADZONE = 3000
    };

    SDL_GameController* GameController = nullptr;
    int16_t ControllerLeftXAxis = 0;
    int16_t ControllerLeftYAxis = 0;
    int16_t ControllerRightXAxis = 0;
    int16_t ControllerRightYAxis = 0;
    uint32_t LastControllerTime = 0;
    float ControllerSpeedBoost = 1;
    bool AnalogScrollActive = false;
    ScrollDirType ScrollDirection = SDIR_NONE;

    /*
    ** Deferred touch input state. Nothing is emitted on finger down; the
    ** gesture decides what mouse events to synthesize. Gestures (adapted from
    ** the PeonPad iPad RTS scheme):
    **   one finger tap    -> left click
    **   one finger drag   -> left-button drag (selection box)
    **   two finger tap    -> right click at the leftmost finger (deselect)
    **   three finger drag -> map scroll
    */
    enum TouchStateType
    {
        TOUCH_IDLE,     // no fingers involved in a gesture
        TOUCH_PENDING,  // one finger down, gesture not yet identified
        TOUCH_DRAGGING, // committed to a left-button drag (selection box)
        TOUCH_TWO,      // two fingers down, pending a right-click tap
        TOUCH_PANNING,  // three-finger map scroll
        TOUCH_CANCELLED // gesture consumed/aborted, waiting for all fingers up
    };

    // Gesture thresholds, measured in window points (not pixels).
    static constexpr float TOUCH_DRAG_THRESHOLD_PT = 12.0f; // one-finger drag commit
    static constexpr float TOUCH_TWO_CANCEL_PT = 16.0f;     // two-finger tap movement tolerance
    static constexpr float TOUCH_PAN_STEP_PT = 3.0f;        // min centroid travel per scroll step
    static constexpr uint32_t TOUCH_PAN_IDLE_MS = 120;      // stop scrolling after this still time

    struct TouchPoint
    {
        SDL_FingerID Id;
        float DownX, DownY; // normalized [0..1] at finger down
        float CurX, CurY;   // normalized [0..1], latest
    };
    static constexpr int MAX_TOUCH = 3;

    TouchStateType TouchState = TOUCH_IDLE;
    TouchPoint Touches[MAX_TOUCH] = {};
    int TouchCount = 0;     // fingers tracked in Touches[] (capped at MAX_TOUCH)
    int RawFingerCount = 0; // all fingers physically down
    float PanPrevNX = 0.0f, PanPrevNY = 0.0f; // last centroid the scroll acted on
    uint32_t PanLastMoveTicks = 0;

    int Find_Touch(SDL_FingerID id) const;
    bool Touch_Centroid(float& nx, float& ny) const;
};
