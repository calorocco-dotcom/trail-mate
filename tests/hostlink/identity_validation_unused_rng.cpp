// Curve25519.cpp also defines dh1(), whose hardware RNG is not used by eval().
// Windows linkers still require those definitions. Fail if the test ever calls
// that path; do not substitute fake random bytes into a cryptographic test.
#include <RNG.h>
#include <cstdlib>
RNGClass::RNGClass() {}
RNGClass::~RNGClass() {}
void RNGClass::rand(uint8_t*, size_t) { std::abort(); }
RNGClass RNG;
