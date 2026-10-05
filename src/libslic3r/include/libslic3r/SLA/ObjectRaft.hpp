#pragma once

#include "Slic3r/Domain/ConfigDefsSLA.hpp" // Domain::sla::RaftType

namespace Slic3r {

/**
 * @brief What raft_type resolves to for one object (M7.8.4, rulebook R6).
 *
 * raft_type names the raft of a print, and one of its values, Auto, does not name a raft at all: it
 * is a decision that is taken per object, from the underside of that object in the pose and the
 * elevation it prints at (libslic3r/SLA/RaftAuto.hpp). This is what that decision came to, and it
 * is what the raft helpers of SLAPrint.hpp and of the support tool take instead of reading raft_type
 * themselves, so that the slice and the support preview cannot come to two different rafts.
 *
 * Empty stands for "nothing was decided", which is every raft type that is not Auto (their helpers
 * read raft_type as it is stored) and an Auto that nothing has resolved yet, which builds no raft:
 * that is the safe half of the rule, R6.1, a raft wastes resin.
 */
struct ObjectRaft
{
    /// The raft type to build. Never Auto, which is a decision and not a shape.
    Domain::sla::RaftType type = Domain::sla::RaftType::None;
    /// Auto found a suction cup under the object, so a raft goes in (R6.2). Only ever true for Auto.
    bool suction = false;
};

} // namespace Slic3r