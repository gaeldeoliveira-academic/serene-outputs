#include <array>
#include <fstream>
#include <iterator>
#include <serene/outputs/pose_recorder.hpp>
/** Exercise the isolated installed pose_recorder component. argv[1] names catalog JSON and argv[2] a fresh sidecar; return zero or a distinct setup/append/finalize code. */
int main(int argc, char **argv) {
    // Read the catalog, open a recorder, append two world-pose frames, and publish completed outcomes.

    if (argc != 3)
        return 2;
    /* Catalog input stream. */ std::ifstream input(argv[1]);
    /* Owned exact catalog JSON bytes. */ std::string catalog((std::istreambuf_iterator<char>(input)), {});
    /* Owning recorder obtained from the installed component. */ auto recorder = serene::outputs::pose::Recorder::open(argv[2], catalog);
    if (!recorder)
        return 3;
    /* Four catalog-ordered sample poses: world xyz metres and unit quaternion wxyz. */ std::array<double, 28> poses{1, 2, 3, 1, 0, 0, 0, 1,  2, 8, 1, 0, 0, 0,
                                 2, 2, 8, 1, 0, 0, 0, -4, 6, 1, 1, 0, 0, 0};
    using namespace serene::outputs::pose;
    if (!recorder->append_frame({Analysis::transient, Event::initial, 1.25, 0, 0}, poses))
        return 4;
    if (!recorder->append_frame({Analysis::transient, Event::target, 1.26, 1, 0}, poses))
        return 5;
    return recorder->finalize({Outcome::completed, Outcome::completed}) ? 0 : 6;
}
