/**
 * @file apple_sorter2.cpp
 * @brief Automated Apple Sorting System — Raspberry Pi Edge Controller
 *
 * This program runs on a Raspberry Pi and orchestrates the full sorting
 * pipeline: IR detection → dual-camera capture → cloud inference via
 * ngrok-tunneled Colab API → servo-based physical sorting.
 *
 * Hardware: L298N motor driver, 3x servo gates, IR proximity sensor,
 *           Raspberry Pi CSI camera, USB webcam.
 *
 * Build:  g++ -o applesortv2 apple_sorter2.cpp -lpigpio -lcurl -lpthread
 * Run:    sudo ./applesortv2
 */

#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <string>
#include <utility>
#include <future>
#include <array>
#include <mutex>

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <curl/curl.h>
#include <pigpio.h>

// ─────────────────────────────────────────────
// Pin Definitions (BCM numbering)
// ─────────────────────────────────────────────
namespace Pins {
    // Motor driver (L298N)
    constexpr int MOTOR_ENA = 12;
    constexpr int MOTOR_IN1 = 27;
    constexpr int MOTOR_IN2 = 22;

    // Servo gates
    constexpr int SERVO_1 = 18;
    constexpr int SERVO_2 = 23;
    constexpr int SERVO_3 = 24;

    // IR proximity sensor
    constexpr int IR_SENSOR = 17;
}

// ─────────────────────────────────────────────
// Tunable Configuration
// ─────────────────────────────────────────────
struct SystemConfig {
    int ir_to_camera_delay_ms  = 800;   ///< Delay after IR trigger before capturing images
    int servo_actuation_ms     = 3000;  ///< How long the servo stays extended

    /// Delay from camera position to each servo gate (belt travel time)
    std::array<int, 3> camera_to_servo_ms = {2000, 3500, 5000};

    /// Colab Flask API endpoint (update each session via ngrok URL)
    std::string backend_url = "https://formidable-olin-unperdurable.ngrok-free.dev/api/classify";
};

static SystemConfig g_config;

// ─────────────────────────────────────────────
// Classification Labels
// ─────────────────────────────────────────────
enum class AppleClass { GOOD, BLOTCH, ROT, SCAB, UNKNOWN, ERROR };

static const char* appleClassToString(AppleClass cls) {
    switch (cls) {
        case AppleClass::GOOD:    return "GOOD (Normal)";
        case AppleClass::BLOTCH:  return "BLOTCH (Disease)";
        case AppleClass::ROT:     return "ROT (Disease)";
        case AppleClass::SCAB:    return "SCAB (Disease)";
        case AppleClass::UNKNOWN: return "UNKNOWN";
        case AppleClass::ERROR:   return "ERROR";
    }
    return "UNKNOWN";
}

// ─────────────────────────────────────────────
// Non-Blocking Keyboard Input (RAII)
// ─────────────────────────────────────────────

/**
 * @brief Sets terminal to raw, non-blocking mode on construction
 *        and restores it on destruction.
 */
class KeyboardInput {
public:
    KeyboardInput() {
        tcgetattr(STDIN_FILENO, &original_);
        struct termios raw = original_;
        raw.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);

        original_flags_ = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, original_flags_ | O_NONBLOCK);
    }

    ~KeyboardInput() {
        tcsetattr(STDIN_FILENO, TCSANOW, &original_);
        fcntl(STDIN_FILENO, F_SETFL, original_flags_);
    }

    /** @return Character code of pressed key, or EOF if none. */
    int getKeyPress() const { return getchar(); }

    // Non-copyable
    KeyboardInput(const KeyboardInput&) = delete;
    KeyboardInput& operator=(const KeyboardInput&) = delete;

private:
    struct termios original_{};
    int original_flags_ = 0;
};

// ─────────────────────────────────────────────
// Motor Controller (L298N H-Bridge)
// ─────────────────────────────────────────────

class MotorController {
public:
    MotorController() {
        gpioSetMode(Pins::MOTOR_ENA, PI_OUTPUT);
        gpioSetMode(Pins::MOTOR_IN1, PI_OUTPUT);
        gpioSetMode(Pins::MOTOR_IN2, PI_OUTPUT);
    }

    void start() {
        if (is_moving_) return;
        std::cout << "[MOTOR] Conveyor started." << std::endl;
        gpioWrite(Pins::MOTOR_IN1, 1);
        gpioWrite(Pins::MOTOR_IN2, 0);
        gpioPWM(Pins::MOTOR_ENA, kPwmSpeed);
        is_moving_ = true;
    }

    void stop() {
        if (!is_moving_) return;
        std::cout << "[MOTOR] Conveyor stopped." << std::endl;
        gpioWrite(Pins::MOTOR_IN1, 0);
        gpioWrite(Pins::MOTOR_IN2, 0);
        gpioPWM(Pins::MOTOR_ENA, 0);
        is_moving_ = false;
    }

private:
    static constexpr int kPwmSpeed = 180;
    bool is_moving_ = false;
};

// ─────────────────────────────────────────────
// Servo Controller (3-Gate Sorting Mechanism)
// ─────────────────────────────────────────────

class ServoController {
public:
    static constexpr int kNumServos = 3;

    ServoController() {
        std::cout << "[SERVO] Initializing all servos to neutral ("
                  << kNeutralAngle << "°)..." << std::endl;
        for (int i = 0; i < kNumServos; ++i) {
            setAngle(pins_[i], kNeutralAngle);
        }
    }

    /**
     * @brief Thread-safe fire-and-retract cycle for a single servo.
     * @param id Servo index (0-based internally, 1-based in UI).
     */
    void actuate(int id) {
        if (id < 1 || id > kNumServos) return;
        int idx = id - 1;

        std::lock_guard<std::mutex> lock(mutexes_[idx]);
        std::cout << "[SERVO " << id << "] FIRING  → " << kFireAngle << "°" << std::endl;
        setAngle(pins_[idx], kFireAngle);

        std::this_thread::sleep_for(
            std::chrono::milliseconds(g_config.servo_actuation_ms));

        std::cout << "[SERVO " << id << "] RETRACT → " << kNeutralAngle << "°" << std::endl;
        setAngle(pins_[idx], kNeutralAngle);
    }

private:
    static constexpr int kNeutralAngle = 70;
    static constexpr int kFireAngle    = 130;
    static constexpr int kSettleMs     = 600;

    std::array<int, kNumServos> pins_ = {Pins::SERVO_1, Pins::SERVO_2, Pins::SERVO_3};
    std::array<std::mutex, kNumServos> mutexes_;

    /** @brief Convert angle (0–180) to PWM pulse width and send to servo. */
    static void setAngle(int pin, int angle) {
        int pulse_us = 500 + (angle * 2000 / 180);
        gpioServo(pin, pulse_us);
        std::this_thread::sleep_for(std::chrono::milliseconds(kSettleMs));
        gpioServo(pin, 0);   // Release to prevent jitter
    }
};

// ─────────────────────────────────────────────
// Dual Camera Capture
// ─────────────────────────────────────────────

class DualCamera {
public:
    /**
     * @brief Capture from both CSI and USB cameras simultaneously.
     * @return Pair of file paths {csi_image, usb_image}.
     */
    std::pair<std::string, std::string> capture() {
        std::cout << "[CAMERA] Triggering dual capture (CSI + USB)..." << std::endl;

        auto csi_future = std::async(std::launch::async, &DualCamera::captureCSI, this);
        auto usb_future = std::async(std::launch::async, &DualCamera::captureUSB, this);

        bool csi_ok = csi_future.get();
        bool usb_ok = usb_future.get();

        if (!csi_ok || !usb_ok) {
            std::cerr << "[CAMERA] Warning: "
                      << (!csi_ok ? "CSI " : "") << (!usb_ok ? "USB " : "")
                      << "capture failed." << std::endl;
        }
        return {kCsiPath, kUsbPath};
    }

private:
    static constexpr const char* kCsiPath = "/tmp/apple_csi.jpg";
    static constexpr const char* kUsbPath = "/tmp/apple_usb.jpg";

    bool captureCSI() {
        std::string cmd = "rpicam-jpeg -n -t 500 -o " + std::string(kCsiPath)
                        + " --width 800 --height 600 > /dev/null 2>&1";
        return system(cmd.c_str()) == 0;
    }

    bool captureUSB() {
        std::string cmd = "fswebcam -r 800x600 --no-banner -d /dev/video1 "
                        + std::string(kUsbPath) + " > /dev/null 2>&1";
        return system(cmd.c_str()) == 0;
    }
};

// ─────────────────────────────────────────────
// Backend API Client (Colab + ngrok)
// ─────────────────────────────────────────────

class BackendClient {
public:
    /**
     * @brief Send dual images to the cloud ViT model for classification.
     * @return Parsed AppleClass result.
     */
    AppleClass classify(const std::string& csi_path, const std::string& usb_path) {
        std::cout << "[NETWORK] Sending dual images to Colab API..." << std::endl;

        CURL* curl = curl_easy_init();
        if (!curl) return AppleClass::ERROR;

        // Build multipart form with both images
        curl_mime* form = curl_mime_init(curl);
        addImageField(form, "image_csi", csi_path);
        addImageField(form, "image_usb", usb_path);

        // Skip ngrok browser warning interstitial
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "ngrok-skip-browser-warning: true");

        // Configure request
        std::string response_body;
        curl_easy_setopt(curl, CURLOPT_URL, g_config.backend_url.c_str());
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

        // Execute and measure latency
        auto t_start = std::chrono::high_resolution_clock::now();
        CURLcode res = curl_easy_perform(curl);
        auto t_end   = std::chrono::high_resolution_clock::now();

        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        auto latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start);

        // Cleanup curl resources
        curl_mime_free(form);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        // Validate response
        if (res != CURLE_OK) {
            std::cerr << "[NETWORK] curl error: " << curl_easy_strerror(res) << std::endl;
            return AppleClass::ERROR;
        }
        if (http_code != 200) {
            std::cerr << "[NETWORK] HTTP " << http_code << ": " << response_body << std::endl;
            return AppleClass::ERROR;
        }

        std::cout << "[NETWORK] Roundtrip: " << latency_ms.count() << " ms" << std::endl;
        std::cout << "[NETWORK] Response:  " << response_body << std::endl;

        return parseClassification(response_body);
    }

private:
    static size_t writeCallback(void* data, size_t size, size_t nmemb, void* userp) {
        static_cast<std::string*>(userp)->append(static_cast<char*>(data), size * nmemb);
        return size * nmemb;
    }

    static void addImageField(curl_mime* form, const char* name, const std::string& path) {
        curl_mimepart* field = curl_mime_addpart(form);
        curl_mime_name(field, name);
        curl_mime_filedata(field, path.c_str());
    }

    /**
     * @brief Parse the JSON response to extract the apple classification.
     *
     * Looks for the "detailed_class" field in the response body.
     * Handles both compact and pretty-printed JSON formatting.
     */
    static AppleClass parseClassification(const std::string& json) {
        struct LabelMapping {
            const char* key;
            AppleClass  cls;
        };

        static const LabelMapping mappings[] = {
            {"Normal_Apple", AppleClass::GOOD},
            {"Blotch_Apple", AppleClass::BLOTCH},
            {"Rot_Apple",    AppleClass::ROT},
            {"Scab_Apple",   AppleClass::SCAB},
        };

        for (const auto& m : mappings) {
            if (json.find(m.key) != std::string::npos) {
                return m.cls;
            }
        }

        std::cerr << "[PARSE] Unrecognized response format." << std::endl;
        return AppleClass::UNKNOWN;
    }
};

// ─────────────────────────────────────────────
// Sorting Logic
// ─────────────────────────────────────────────

/**
 * @brief Map a classification result to a servo ID (1-indexed).
 * @return Servo ID to fire, or 0 if the apple should pass through.
 */
static int getServoForClass(AppleClass cls) {
    switch (cls) {
        case AppleClass::BLOTCH: return 1;
        case AppleClass::ROT:    return 2;
        case AppleClass::SCAB:   return 3;
        default:                 return 0;  // GOOD / UNKNOWN / ERROR → pass through
    }
}

/**
 * @brief Schedule a delayed servo actuation on a background thread.
 *
 * The delay accounts for the belt travel time between the camera
 * position and the physical servo gate location.
 */
static void scheduleServoActuation(ServoController& servo, int servo_id) {
    int delay_ms = g_config.camera_to_servo_ms[servo_id - 1];

    std::thread([&servo, servo_id, delay_ms]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        servo.actuate(servo_id);
    }).detach();
}

// ─────────────────────────────────────────────
// Main Loop
// ─────────────────────────────────────────────

enum class SystemMode { AUTO, MANUAL };

int main() {
    // Initialize GPIO
    if (gpioInitialise() < 0) {
        std::cerr << "pigpio init failed. Run with: sudo ./applesortv2" << std::endl;
        return 1;
    }

    curl_global_init(CURL_GLOBAL_ALL);

    std::cout << "\n=== Apple Sorting System Initialized ===" << std::endl;
    std::cout << "Controls: [m] Toggle Mode | [q] Quit" << std::endl;
    std::cout << "Manual:   [f] Forward | [s] Stop | [c] Capture & Classify"  << std::endl;
    std::cout << "          [1/2/3] Test Servos\n" << std::endl;

    // Initialize subsystems
    KeyboardInput   keyboard;
    MotorController motor;
    ServoController servo;
    DualCamera      camera;
    BackendClient   backend;

    gpioSetMode(Pins::IR_SENSOR, PI_INPUT);

    SystemMode mode = SystemMode::MANUAL;
    std::atomic<bool> running(true);
    bool last_ir_state = false;

    // ── Main event loop ──
    while (running) {
        // ── Handle keyboard input ──
        int key = keyboard.getKeyPress();
        if (key != EOF) {
            switch (key) {
                case 'q':
                    std::cout << "\nShutting down..." << std::endl;
                    motor.stop();
                    running = false;
                    break;

                case 'm':
                    mode = (mode == SystemMode::AUTO) ? SystemMode::MANUAL : SystemMode::AUTO;
                    std::cout << "\n*** MODE: "
                              << (mode == SystemMode::AUTO ? "AUTO" : "MANUAL")
                              << " ***\n" << std::endl;
                    if (mode == SystemMode::MANUAL) motor.stop();
                    break;

                case 'f':
                    if (mode == SystemMode::MANUAL) motor.start();
                    break;

                case 's':
                    if (mode == SystemMode::MANUAL) motor.stop();
                    break;

                case '1': case '2': case '3':
                    if (mode == SystemMode::MANUAL) {
                        int id = key - '0';
                        std::thread(&ServoController::actuate, &servo, id).detach();
                    }
                    break;

                case 'c':
                    if (mode == SystemMode::MANUAL) {
                        auto [csi, usb] = camera.capture();
                        AppleClass result = backend.classify(csi, usb);
                        std::cout << "[RESULT] " << appleClassToString(result) << std::endl;
                    }
                    break;
            }
        }

        // ── Read IR sensor (active-low: beam broken = object detected) ──
        bool ir_triggered = (gpioRead(Pins::IR_SENSOR) == 0);
        bool rising_edge  = ir_triggered && !last_ir_state;

        if (rising_edge) {
            std::cout << "\n[SENSOR] Apple detected! (IR beam broken)" << std::endl;
        }

        // ── Autonomous sorting pipeline ──
        if (mode == SystemMode::AUTO && running) {
            motor.start();

            if (rising_edge) {
                // Wait for apple to reach camera position, then stop belt
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(g_config.ir_to_camera_delay_ms));
                motor.stop();

                // Capture + classify
                auto [csi, usb] = camera.capture();
                AppleClass result = backend.classify(csi, usb);
                int servo_id = getServoForClass(result);

                std::cout << "[SORTING] " << appleClassToString(result);
                if (servo_id > 0) {
                    std::cout << " → Servo " << servo_id << std::endl;
                    scheduleServoActuation(servo, servo_id);
                } else {
                    std::cout << " → Passing through." << std::endl;
                }

                // Restart belt immediately (servo fires on a delayed thread)
                motor.start();
            }
        }

        last_ir_state = ir_triggered;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Cleanup
    curl_global_cleanup();
    gpioTerminate();
    return 0;
}
