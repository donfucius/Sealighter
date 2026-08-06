#include <gtest/gtest.h>
#include "logger.h"

namespace {

class SealighterTestEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        logger::Logger::GetInstance().init("sealighter_tests");
    }
};

}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new SealighterTestEnvironment());
    return RUN_ALL_TESTS();
}
