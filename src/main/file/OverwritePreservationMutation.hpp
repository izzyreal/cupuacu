#pragma once

#include <string>

namespace cupuacu
{
    struct DocumentSession;
}

namespace cupuacu::file
{
    enum class OverwritePreservationMutationImpact
    {
        Compatible,
        Incompatible,
    };

    struct OverwritePreservationMutation
    {
        OverwritePreservationMutationImpact impact =
            OverwritePreservationMutationImpact::Compatible;
        std::string reason;
    };

    class OverwritePreservationMutationHelper
    {
    public:
        [[nodiscard]] static OverwritePreservationMutation compatible()
        {
            return {};
        }

        [[nodiscard]] static OverwritePreservationMutation
        incompatible(std::string reason)
        {
            return {.impact = OverwritePreservationMutationImpact::Incompatible,
                    .reason = std::move(reason)};
        }

        static void applyToSession(cupuacu::DocumentSession &session,
                                   const OverwritePreservationMutation &mutation)
        {
            if (mutation.impact ==
                OverwritePreservationMutationImpact::Incompatible)
            {
                session.breakOverwritePreservation(mutation.reason);
            }
        }

        static void revertOnSession(cupuacu::DocumentSession &session,
                                    const OverwritePreservationMutation &mutation)
        {
            if (mutation.impact ==
                OverwritePreservationMutationImpact::Incompatible)
            {
                session.clearOverwritePreservationBreak();
            }
        }
    };
} // namespace cupuacu::file
