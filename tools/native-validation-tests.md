# Native state and compression validation regressions

Run from the Chunklet checkout. The random-state test includes the production
constructor comparator and rejects state-only divergence in both recognized
Xoroshiro words in both constructor modes. Exact 24-byte objects ensure an
AddressSanitizer run also catches accidental legacy-sized reads. Legacy engine
state is compared while its vptr and cloned engine self-pointer remain excluded.

```console
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -ffunction-sections -fdata-sections -Isrc tools/random_state_test.cpp -Wl,--gc-sections -o .artifacts/random_state_test
ASAN_OPTIONS=detect_leaks=0 .artifacts/random_state_test
bash tools/build-zlib-proof.sh .artifacts/build-state-guard
```

`detect_leaks=0` avoids LeakSanitizer's unsupported ptrace check in the managed
runner; AddressSanitizer bounds checks and UndefinedBehaviorSanitizer stay on.

The compression runner uses the existing pinned build's zlib-ng archive and an
independently loaded system zlib at the native ABI boundary. Its sampling test
first sends 16 unsupported inputs, injects one candidate compression failure,
then requires all 16 successful round-trip validations. A concurrent phase
holds 16 candidate calls while eight excess calls must finish via the native
fallback; releasing the held calls must complete exactly 16 validations.
The existing 50-case wire proof and synthetic benchmark then run unchanged.

These checks prove comparison bounds and sampling behavior. Exact BDS live
validation, generation, stop, and restart/save decoding remain runtime gates.
