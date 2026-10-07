/** @file strategy.c
 * @brief Private RNG primitives and painting-choice implementation.
 */

#include "strategy.h"

#include "internal/model.h"

/** @details Names have static storage and match the CLI/config spellings.
 */
const char *strategy_name(Strategy strategy) {
    static const char *names[] = {"ordered", "random", "least-crowded",
                                  "cyclic"};
    return names[strategy];
}

/* SplitMix64 arithmetic is deliberately fixed: unsigned wraparound is defined.
 */
/** @brief Apply the fixed SplitMix64 finalizer.
 * @param value Input word.
 * @return Deterministically mixed 64-bit word.
 * @details Unsigned wraparound is intentional and defined; not cryptography.
 */
static uint64_t mix(uint64_t value) {
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

/** @brief Advance a private SplitMix64 stream by one word.
 * @param[in,out] state Visitor/sample-private RNG state.
 * @return Next mixed 64-bit word.
 */
static uint64_t next_random(uint64_t *state) {
    *state += UINT64_C(0x9e3779b97f4a7c15);
    return mix(*state);
}

/* Rejection sampling avoids bias from a simple random % bound. */
/** @brief Sample uniformly using rejection rather than biased modulo reduction.
 * @param[in,out] state Private RNG state, advanced as needed.
 * @param bound Exclusive upper bound; must be greater than zero.
 * @return Value in [0, bound).
 */
static uint64_t bounded(uint64_t *state, uint64_t bound) {
    uint64_t threshold = (UINT64_C(0) - bound) % bound;
    uint64_t value;
    do {
        value = next_random(state);
    } while (value < threshold);
    return value % bound;
}

/** @brief Sample an exogenous input independently of painting-choice streams.
 * @param c Configuration supplying the seed.
 * @param id Visitor ID.
 * @param painting Input's painting key (0 for arrivals).
 * @param tag Domain-separation constant for the input kind.
 * @param low Inclusive lower bound.
 * @param high Inclusive upper bound, at least low.
 * @return Deterministic sample in [low, high].
 * @pre high - low + 1 is nonzero and representable.
 */
static uint64_t input_sample(const Config *c, uint64_t id, uint64_t painting,
                             uint64_t tag, uint64_t low, uint64_t high) {
    uint64_t state = mix(c->seed ^ mix(id) ^ mix(painting + tag));
    return low + bounded(&state, high - low + 1);
}

/** @details Uses a dedicated arrival-domain tag and visitor ID key.
 */
uint64_t arrival_interval(const Config *config, uint64_t id) {
    return input_sample(config, id, 0, UINT64_C(0x11ad), config->arrival_min,
                        config->arrival_max);
}

/** @details Uses a separate viewing-domain tag and a one-based painting key.
 */
uint64_t viewing_duration(const Config *config, uint64_t id, int painting) {
    return input_sample(config, id, (uint64_t)painting + 1, UINT64_C(0x22be),
                        config->view_min, config->view_max);
}

/** @brief Compute the greatest common divisor with Euclid's algorithm.
 * @param a First unsigned operand.
 * @param b Second unsigned operand.
 * @return Greatest common divisor; 0 if both operands are 0.
 */
static uint64_t gcd(uint64_t a, uint64_t b) {
    while (b) {
        uint64_t remainder = a % b;
        a = b;
        b = remainder;
    }
    return a;
}

/** @details Ordered/random scan unseen paintings; least-crowded observes
 * viewers plus waiters with index ties. Cyclic uses initialized coprime
 * traversal.
 */
int choose_painting(const Simulation *simulation, Visitor *visitor) {
    uint64_t count = simulation->config->paintings;
    if (simulation->config->strategy == CYCLIC)
        return (int)((visitor->start + visitor->viewed * visitor->step) %
                     count);
    uint64_t rank = simulation->config->strategy == RANDOM
                        ? bounded(&visitor->rng, count - visitor->viewed)
                        : 0;
    int best = -1;
    uint64_t smallest = UINT64_MAX;
    for (uint64_t p = 0; p < count; ++p) {
        if (visitor->seen[p])
            continue;
        if (simulation->config->strategy == LEAST_CROWDED) {
            uint64_t crowd = simulation->paintings[p].viewers +
                             simulation->paintings[p].queue.length;
            if (crowd < smallest) {
                smallest = crowd;
                best = (int)p;
            }
        } else if (!rank--)
            return (int)p;
    }
    return best;
}

/** @details Seeds the visitor-private stream from seed/ID, chooses the cyclic
 * start, and adjusts the step until it is coprime to the painting count.
 */
void strategy_init(const Config *config, Visitor *visitor) {
    visitor->rng = mix(config->seed ^ mix(visitor->id) ^ UINT64_C(0x33cf));
    visitor->start = bounded(&visitor->rng, config->paintings);
    visitor->step = 0;
    if (config->paintings > 1) {
        visitor->step = 1 + bounded(&visitor->rng, config->paintings - 1);
        while (gcd(visitor->step, config->paintings) != 1)
            visitor->step = visitor->step % (config->paintings - 1) + 1;
    }
}
