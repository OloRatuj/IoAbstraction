
#ifndef IOA_RESISTIVETOUCHSCREEN_H
#define IOA_RESISTIVETOUCHSCREEN_H

#include "PlatformDetermination.h"
#include "AnalogDeviceAbstraction.h"
#include <TaskManagerIO.h>

/**
 * @file ResistiveTouchScreen.h A
 * @brief simple resistive touch screen class that converts touch coordinates into a range between floating point values 0.0 and 1.0.
 */

#ifndef TOUCH_THRESHOLD
#define TOUCH_THRESHOLD 0.05F
#endif

namespace iotouch {
    /** internal acceleration modes used by the acceleration handler. */
    enum AccelerationMode: uint8_t {
        WAITING,
        ACCELERATING,
        NEVER_ACCELERATES
    };

    /**
     * Handles acceleration for touch screens, it can be configured with the number of ticks to wait before starting
     * and if it should accelerate.
     */
    class AccelerationHandler {
    private:
        uint8_t minTicks;
        uint8_t ticks;
        uint8_t accel;
        AccelerationMode mode;
    public:
        AccelerationHandler(uint8_t minTicks, bool accelerate) : minTicks(minTicks) {
            mode = accelerate ? WAITING : NEVER_ACCELERATES;
        }

        void reset() {
            if(mode == ACCELERATING) mode = WAITING;
        }

        bool tick();
    };

    /**
     * Provides calibration for IoAbstraction based touch facilities, it does so by recording the minimum and maximum
     * values in the X and Y dimension and then correction values to fall within those ranges
     */
    class CalibrationHandler {
    private:
        float minX, maxX;
        float minY, maxY;
        bool calibrationOn;

    public:
        CalibrationHandler() = default;
        CalibrationHandler(const CalibrationHandler& other) = default;
        CalibrationHandler& operator=(const CalibrationHandler& other) = default;

        void setCalibrationValues(float mnX, float mxX, float mnY, float mxY);

        void enableCalibration(bool state) {
            calibrationOn = state;
        }

        float calibrateX(float rawValue, bool isInverted) const;

        float calibrateY(float rawValue, bool isInverted) const;

        float getMinX() const { return minX;}
        float getMinY() const { return minY;}
        float getMaxX() const { return maxX;}
        float getMaxY() const { return maxY;}
    };

    /** records the current state of the touch panel, IE not touched, touched, held or debouncing. */
    enum TouchState : uint8_t {
        /** no touch has been detected */
        NOT_TOUCHED,
        /** the display has been touched */
        TOUCHED,
        /** the touch is being dragged or held */
        HELD,
        /** a debounce is needed */
        TOUCH_DEBOUNCE
    };

#define portableFloatAbs(x) ((x)<0.0F?-(x):(x))

    /**
     * A touch integrator is a class that is capable of receiving touch events from a touch panel and reporting those
     * said events to the touch screen manager. It is a pull API in that `internalProcessTouch` will be called to pull
     * the value from this class as needed.
     */
    class TouchInterrogator {
    public:
        enum TouchRotation : uint8_t {
            PORTRAIT,
            PORTRAIT_INVERTED,
            LANDSCAPE,
            LANDSCAPE_INVERTED,
            RAW
        };

        /**
         * called by the touch screen manager to get the latest touch information.
         * @param ptrX a pointer to be populated with the X position between 0 and 1
         * @param ptrY a pointer to be popualted with the Y position between 0 and 1
         * @param rotation  the rotation of the display, one of the above.
         * @param calib the calibration handler that can be used to adjust the values before returning from this method
         * @return the touch state after this call
         */
        virtual TouchState internalProcessTouch(float* ptrX, float* ptrY, TouchRotation rotation, const CalibrationHandler& calib)=0;
    };

    class TouchInterrogator;

    class TouchScreenManager : public Executable {
    public:
    private:
        AccelerationHandler accelerationHandler;
        CalibrationHandler calibrator;
        TouchInterrogator* touchInterrogator;
        TouchState touchMode;
        bool usedForScrolling = false;
        TouchInterrogator::TouchRotation rotation;
    public:
        explicit TouchScreenManager(TouchInterrogator* interrogator, TouchInterrogator::TouchRotation rot) :
                accelerationHandler(10, true), calibrator(),
                touchInterrogator(interrogator), touchMode(NOT_TOUCHED), rotation(rot) {}

        void start() {
            touchMode = NOT_TOUCHED;
            taskManager.execute(this);
        }

        void setUsedForScrolling(bool scrolling) {
            usedForScrolling = scrolling;
        }

        void calibrateMinMaxValues(float xmin, float xmax, float ymin, float ymax) {
            calibrator.setCalibrationValues(xmin, xmax, ymin, ymax);
        }

        void setCalibration(const CalibrationHandler& other) { calibrator = other; }

        void enableCalibration(bool ena) {
            calibrator.enableCalibration(ena);
        }

        TouchInterrogator::TouchRotation changeRotation(TouchInterrogator::TouchRotation newRotation);

        TouchInterrogator::TouchRotation getRotation() { return rotation; }

        void exec() override;

        /**
         * You must create a subclass extends from this and takes the three values converting into an
         * event for processing.
         *
         * @param locationX the location between 0 and 1 in the X domain
         * @param locationY the location between 0 and 1 in the Y domain
         * @param touched if the panel is current touched
         */
        virtual void sendEvent(float locationX, float locationY, float touchPressure, TouchState touched) = 0;
    };

    /**
     * This class handles the basics of a touch screen interface, capturing the values and converting them into a usable
     * form, it is pure abstract and the sendEvent needs implementing with a suitable implemetnation for your needs.
     * It is heavily based on the Adafruit TouchScreen library but modified to work with AnalogDevice so that it can
     * work reliably across a wider range of devices.
     *
     * Important notes
     *
     * * all the GPIOs used must be OUTPUT capable, this matters on some boards such as ESP32
     * * Y+ and X- must be connected to ADC (analog input capable) pins.
     * * it uses taskManager and takes readings at the millisecond interval provided.
     */
    class ResistiveTouchInterrogator : public TouchInterrogator {
    private:
        pinid_t xpPin, xnPinAdc, ypPinAdc, ynPin;
    public:

        ResistiveTouchInterrogator(pinid_t xpPin, pinid_t xnPin, pinid_t ypPin, pinid_t ynPin)
                : xpPin(xpPin), xnPinAdc(xnPin), ypPinAdc(ypPin), ynPin(ynPin) {}

        TouchState internalProcessTouch(float* ptrX, float* ptrY, TouchRotation rotation, const CalibrationHandler& calibrator) override;

    };

    /**
     * Handles the touch screen interface by storing the latest values for later use. This allows for simple uses of
     * touch screen without creating a sub class, by just getting the latest values as needed.
     */
    class ValueStoringResistiveTouchScreen : public TouchScreenManager {
    private:
        float lastX, lastY, touchPressure;
        TouchState touchState;
    public:
        ValueStoringResistiveTouchScreen(TouchInterrogator& interrogator, TouchInterrogator::TouchRotation rotation)
            : TouchScreenManager(&interrogator, rotation), lastX(0.0F), lastY(0.0F), touchState(NOT_TOUCHED) {
        }

        void sendEvent(float locationX, float locationY, float pressure, TouchState touched) override;

        float getTouchPressure() const { return touchPressure; }
        float getLastX() const { return lastX; }
        float getLastY() const { return lastY; }
        bool isPressed() const { return touchState == TOUCHED; }
        TouchState getTouchState() const { return touchState; }
    };
}

#endif //IOA_RESISTIVETOUCHSCREEN_H
