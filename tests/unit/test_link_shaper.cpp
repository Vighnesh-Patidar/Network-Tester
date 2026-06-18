#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "chaos_engine/system_execution_engine.h"
#include "substrate/link_shaper.h"

namespace {

bool chain_contains_sequence(
    const std::vector<std::vector<std::string>>& commands,
    const std::vector<std::string>& needle) {
    for (const auto& cmd : commands) {
        for (std::size_t i = 0; i + needle.size() <= cmd.size(); ++i) {
            bool match = true;
            for (std::size_t j = 0; j < needle.size(); ++j) {
                if (cmd[i + j] != needle[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return true;
            }
        }
    }
    return false;
}

bool chain_contains_token(const std::vector<std::vector<std::string>>& commands,
                          const std::string& token) {
    return chain_contains_sequence(commands, {token});
}

}  // namespace

TEST(LinkShaper, BuildsTbfThenNetemForShapedLink) {
    nch::SystemExecutionEngine engine(true);
    nch::LinkShaper shaper(engine);

    nch::Link link;
    link.id = "l1";
    link.capacity_mbps = 100;
    link.latency_ms = 5.0;
    link.jitter_ms = 1.0;
    link.loss_pct = 0.5;

    const auto commands = shaper.build_apply_commands(link, "r1-eth0");

    // tbf is the outer/root qdisc; netem is nested beneath it (§5.6).
    EXPECT_TRUE(chain_contains_sequence(commands, {"root", "handle", "1:", "tbf"}));
    EXPECT_TRUE(chain_contains_sequence(commands, {"rate", "100mbit"}));
    EXPECT_TRUE(chain_contains_sequence(commands, {"parent", "1:", "handle", "10:"}));
    EXPECT_TRUE(chain_contains_sequence(commands, {"delay", "5ms", "1ms"}));
    EXPECT_TRUE(chain_contains_sequence(commands, {"loss", "0.5%"}));
}

TEST(LinkShaper, UsesNetemAsRootWhenUnshaped) {
    nch::SystemExecutionEngine engine(true);
    nch::LinkShaper shaper(engine);

    nch::Link link;
    link.id = "l1";
    link.capacity_mbps = 0;  // unshaped capacity
    link.latency_ms = 2.0;

    const auto commands = shaper.build_apply_commands(link, "r2-eth1");
    EXPECT_FALSE(chain_contains_token(commands, "tbf"));
    EXPECT_TRUE(chain_contains_sequence(commands, {"root", "handle", "10:", "netem"}));
    EXPECT_TRUE(chain_contains_sequence(commands, {"delay", "2ms"}));
}

TEST(LinkShaper, OmitsLossClauseWhenZero) {
    nch::SystemExecutionEngine engine(true);
    nch::LinkShaper shaper(engine);

    nch::Link link;
    link.id = "l1";
    link.capacity_mbps = 0;
    link.latency_ms = 3.0;
    link.loss_pct = 0.0;

    const auto commands = shaper.build_apply_commands(link, "r3-eth0");
    EXPECT_FALSE(chain_contains_token(commands, "loss"));
}

TEST(LinkShaper, UpdateUsesReplaceNotAdd) {
    nch::SystemExecutionEngine engine(true);
    nch::LinkShaper shaper(engine);

    nch::Link link;
    link.id = "l1";
    link.capacity_mbps = 100;
    link.loss_pct = 15.0;

    const auto commands = shaper.build_update_commands(link, "r1-eth0");
    EXPECT_TRUE(chain_contains_token(commands, "replace"));
    EXPECT_FALSE(chain_contains_token(commands, "add"));
    EXPECT_TRUE(chain_contains_sequence(commands, {"loss", "15%"}));
}

TEST(LinkShaper, ApplyStartsByClearingRootQdisc) {
    nch::SystemExecutionEngine engine(true);
    nch::LinkShaper shaper(engine);

    nch::Link link;
    link.id = "l1";
    link.capacity_mbps = 10;

    const auto commands = shaper.build_apply_commands(link, "r1-eth0");
    ASSERT_FALSE(commands.empty());
    EXPECT_EQ(commands.front(),
              (std::vector<std::string>{"tc", "qdisc", "del", "dev", "r1-eth0", "root"}));
}
