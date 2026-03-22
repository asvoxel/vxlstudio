#include <gtest/gtest.h>

#include "vxl/io.h"

namespace vxl {
namespace {

// ---------------------------------------------------------------------------
// SimIO: set_output / get_input loopback
// ---------------------------------------------------------------------------
TEST(SimIO, SetOutputGetInputLoopback) {
    auto result = IOManager::open("sim://io");
    ASSERT_TRUE(result.ok());
    auto& io = result.value;

    EXPECT_TRUE(io->is_open());
    EXPECT_EQ(io->device_id(), "SIM-IO");

    // Set some outputs
    EXPECT_TRUE(io->set_output("Y0", true).ok());
    EXPECT_TRUE(io->set_output("Y1", false).ok());
    EXPECT_TRUE(io->set_output("X0", true).ok());

    // Read them back (loopback)
    auto r0 = io->get_input("Y0");
    ASSERT_TRUE(r0.ok());
    EXPECT_TRUE(r0.value);

    auto r1 = io->get_input("Y1");
    ASSERT_TRUE(r1.ok());
    EXPECT_FALSE(r1.value);

    auto r2 = io->get_input("X0");
    ASSERT_TRUE(r2.ok());
    EXPECT_TRUE(r2.value);

    // Unset pin returns false by default
    auto r3 = io->get_input("X99");
    ASSERT_TRUE(r3.ok());
    EXPECT_FALSE(r3.value);
}

// ---------------------------------------------------------------------------
// SimIO: write_register / read_register
// ---------------------------------------------------------------------------
TEST(SimIO, WriteReadRegister) {
    auto result = IOManager::open("sim://io");
    ASSERT_TRUE(result.ok());
    auto& io = result.value;

    EXPECT_TRUE(io->write_register(100, 0x1234).ok());
    EXPECT_TRUE(io->write_register(200, 0xABCD).ok());

    auto r1 = io->read_register(100);
    ASSERT_TRUE(r1.ok());
    EXPECT_EQ(r1.value, 0x1234);

    auto r2 = io->read_register(200);
    ASSERT_TRUE(r2.ok());
    EXPECT_EQ(r2.value, 0xABCD);

    // Unset register returns 0
    auto r3 = io->read_register(999);
    ASSERT_TRUE(r3.ok());
    EXPECT_EQ(r3.value, 0);
}

// ---------------------------------------------------------------------------
// SimIO: close prevents operations
// ---------------------------------------------------------------------------
TEST(SimIO, CloseAndReopen) {
    auto result = IOManager::open("sim://io");
    ASSERT_TRUE(result.ok());
    auto& io = result.value;

    EXPECT_TRUE(io->is_open());
    io->close();
    EXPECT_FALSE(io->is_open());

    // Operations should fail when closed
    EXPECT_FALSE(io->set_output("Y0", true).ok());
    EXPECT_FALSE(io->get_input("X0").ok());
    EXPECT_FALSE(io->write_register(0, 42).ok());
    EXPECT_FALSE(io->read_register(0).ok());

    // Reopen should work
    auto r = io->open();
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(io->is_open());
}

// ---------------------------------------------------------------------------
// IOManager: open("sim://io") succeeds
// ---------------------------------------------------------------------------
TEST(IOManager, OpenSimSucceeds) {
    auto result = IOManager::open("sim://io");
    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.value, nullptr);
    EXPECT_TRUE(result.value->is_open());
}

// ---------------------------------------------------------------------------
// IOManager: open("modbus-tcp://invalid") fails gracefully
// ---------------------------------------------------------------------------
TEST(IOManager, OpenModbusInvalidFailsGracefully) {
    // Use a non-routable address with short timeout to ensure quick failure
    auto result = IOManager::open("modbus-tcp://192.0.2.1:502");
    EXPECT_FALSE(result.ok());
    // Should be a connection error, not a crash
    EXPECT_TRUE(result.code == ErrorCode::IO_CONNECTION_FAILED ||
                result.code == ErrorCode::DEVICE_TIMEOUT);
}

// ---------------------------------------------------------------------------
// IOManager: open with unknown scheme fails
// ---------------------------------------------------------------------------
TEST(IOManager, OpenUnknownSchemeFails) {
    auto result = IOManager::open("foobar://something");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.code, ErrorCode::INVALID_PARAMETER);
}

// ---------------------------------------------------------------------------
// IOManager: enumerate() includes "sim://io"
// ---------------------------------------------------------------------------
TEST(IOManager, EnumerateIncludesSimIO) {
    auto devices = IOManager::enumerate();
    ASSERT_FALSE(devices.empty());
    EXPECT_EQ(devices[0], "sim://io");
}

} // namespace
} // namespace vxl
