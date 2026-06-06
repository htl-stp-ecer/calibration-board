#include "calib_bridge/Publisher.h"
#include "calib_bridge/Channels.h"

#include <raccoon/Transport.h>
#include <raccoon/scalar_f_t.hpp>
#include <raccoon/scalar_i32_t.hpp>
#include <raccoon/string_t.hpp>
#include <raccoon/vector3f_t.hpp>

#include <chrono>

namespace calib_bridge
{
    namespace
    {
        int64_t now_us()
        {
            using namespace std::chrono;
            return duration_cast<microseconds>(
                system_clock::now().time_since_epoch()).count();
        }
    }

    struct Publisher::Impl
    {
        raccoon::Transport transport;
        explicit Impl(const std::string& provider)
            : transport(raccoon::Transport::create(provider)) {}
    };

    Publisher::Publisher(const std::string& provider)
        : impl_(std::make_unique<Impl>(provider)) {}

    Publisher::~Publisher() = default;

    void Publisher::publish_icm(const framing::IcmSample& s)
    {
        const int64_t ts = now_us();

        raccoon::vector3f_t accel{};
        accel.timestamp = ts;
        accel.x = s.ax_g();
        accel.y = s.ay_g();
        accel.z = s.az_g();
        impl_->transport.publish(Channels::ICM_ACCEL, accel);

        raccoon::vector3f_t gyro{};
        gyro.timestamp = ts;
        gyro.x = s.gx_dps();
        gyro.y = s.gy_dps();
        gyro.z = s.gz_dps();
        impl_->transport.publish(Channels::ICM_GYRO, gyro);

        raccoon::scalar_f_t temp{};
        temp.timestamp = ts;
        temp.value = s.temp_c();
        impl_->transport.publish(Channels::ICM_TEMP, temp);
    }

    void Publisher::publish_paa(const framing::PaaSample& s)
    {
        const int64_t ts = now_us();

        auto pub_i32 = [&](const char* ch, int32_t v) {
            raccoon::scalar_i32_t m{};
            m.timestamp = ts;
            m.value = v;
            impl_->transport.publish(ch, m);
        };
        pub_i32(Channels::PAA_DX,      s.dx);
        pub_i32(Channels::PAA_DY,      s.dy);
        pub_i32(Channels::PAA_SQUAL,   s.squal);
        pub_i32(Channels::PAA_SHUTTER, s.shutter);
        pub_i32(Channels::PAA_MOTION,  s.motion);
    }

    namespace
    {
        void publish_string(raccoon::Transport& t, const char* channel, const std::string& v)
        {
            raccoon::string_t m{};
            m.timestamp = now_us();
            m.value = v;
            t.publish(channel, m);
        }
    }

    void Publisher::publish_board_status(const std::string& state, const std::string& port)
    {
        publish_string(impl_->transport, Channels::STATUS_BOARD, state);
        publish_string(impl_->transport, Channels::STATUS_PORT,  port);
    }

    void Publisher::publish_icm_status(const std::string& state)
    {
        publish_string(impl_->transport, Channels::STATUS_ICM, state);
    }

    void Publisher::publish_paa_status(const std::string& state)
    {
        publish_string(impl_->transport, Channels::STATUS_PAA, state);
    }

    void Publisher::publish_stats(const std::string& json)
    {
        publish_string(impl_->transport, Channels::STATUS_STATS, json);
    }

    void Publisher::publish_paa_cal(const framing::PaaCalFrame& s)
    {
        const int64_t ts = now_us();
        auto pub_f = [&](const char* ch, float v) {
            raccoon::scalar_f_t m{};
            m.timestamp = ts;
            m.value = v;
            impl_->transport.publish(ch, m);
        };
        pub_f(Channels::PAA_CAL_CX,     s.cx_per_cm);
        pub_f(Channels::PAA_CAL_CY,     s.cy_per_cm);
        pub_f(Channels::PAA_CAL_HEIGHT, s.height_mm);

        raccoon::scalar_i32_t v{};
        v.timestamp = ts;
        v.value = s.valid ? 1 : 0;
        impl_->transport.publish(Channels::PAA_CAL_VALID, v);
    }

    void Publisher::publish_paa_cm(float dx_cm, float dy_cm, float pos_x_cm, float pos_y_cm)
    {
        const int64_t ts = now_us();
        auto pub_f = [&](const char* ch, float v) {
            raccoon::scalar_f_t m{};
            m.timestamp = ts;
            m.value = v;
            impl_->transport.publish(ch, m);
        };
        pub_f(Channels::PAA_CM_X,     dx_cm);
        pub_f(Channels::PAA_CM_Y,     dy_cm);
        pub_f(Channels::PAA_CM_POS_X, pos_x_cm);
        pub_f(Channels::PAA_CM_POS_Y, pos_y_cm);
    }

    void Publisher::publish_orientation(const framing::OrientationFrame& s,
                                         float roll, float pitch, float yaw)
    {
        const int64_t ts = now_us();
        auto pub_f = [&](const char* ch, float v) {
            raccoon::scalar_f_t m{};
            m.timestamp = ts;
            m.value = v;
            impl_->transport.publish(ch, m);
        };
        pub_f(Channels::ICM_QUAT_W, s.qw);
        pub_f(Channels::ICM_QUAT_X, s.qx);
        pub_f(Channels::ICM_QUAT_Y, s.qy);
        pub_f(Channels::ICM_QUAT_Z, s.qz);
        pub_f(Channels::ICM_EULER_ROLL,  roll);
        pub_f(Channels::ICM_EULER_PITCH, pitch);
        pub_f(Channels::ICM_EULER_YAW,   yaw);

        raccoon::vector3f_t g{};
        g.timestamp = ts;
        g.x = s.gx_dps; g.y = s.gy_dps; g.z = s.gz_dps;
        impl_->transport.publish(Channels::ICM_GYRO_CORR, g);

        raccoon::vector3f_t b{};
        b.timestamp = ts;
        b.x = s.bias_x_dps; b.y = s.bias_y_dps; b.z = s.bias_z_dps;
        impl_->transport.publish(Channels::ICM_GYRO_BIAS, b);

        raccoon::scalar_i32_t r{};
        r.timestamp = ts;
        r.value = s.at_rest ? 1 : 0;
        impl_->transport.publish(Channels::ICM_AT_REST, r);

        raccoon::scalar_i32_t v{};
        v.timestamp = ts;
        v.value = s.bias_persisted ? 1 : 0;
        impl_->transport.publish(Channels::ICM_BIAS_VALID, v);
    }

    void Publisher::publish_odom(float pos_x_cm, float pos_y_cm, float heading_deg)
    {
        const int64_t ts = now_us();
        auto pub_f = [&](const char* ch, float v) {
            raccoon::scalar_f_t m{};
            m.timestamp = ts;
            m.value = v;
            impl_->transport.publish(ch, m);
        };
        pub_f(Channels::ODOM_POS_X,   pos_x_cm);
        pub_f(Channels::ODOM_POS_Y,   pos_y_cm);
        pub_f(Channels::ODOM_HEADING, heading_deg);
    }

    raccoon::Transport& Publisher::transport()
    {
        return impl_->transport;
    }
}
