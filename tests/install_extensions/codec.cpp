#include <serene/outputs/codec.hpp>
#include <array>
/** Exercise installed 0.2.0 fixed-accuracy encode/decode/verification; return zero or a failed-stage code. */
int main(){
    // Create a fixed profile, populate deterministic values, encode/decode, and independently verify the returned values.

  using namespace serene::outputs;
  /* Requested immutable absolute-tolerance profile. */ auto profile=CodecProfile::fixed_accuracy(1e-4);/* Codec sized for the 32-value frame. */ auto codec=Codec::create(32);if(!profile||!codec)return 1;
  /* Owned source, scratch and restored buffers. */ std::array<double,32> values{},scratch{},restored{};for(/* Source value index. */ unsigned i=0;i<values.size();++i)values[i]=.123456789*i+.3333333333333333;
  /* Verified encoded frame borrowing codec storage. */ auto frame=codec->encode(values,scratch,*profile);if(!frame||frame->verified_count!=values.size())return 2;
  if(!codec->decode(frame->encoding,values.size(),frame->bytes,restored,*profile))return 3;
  /* Independent exact-real comparison against the admitted profile. */ auto verified=Codec::verify_values(values,restored,*profile);return verified?0:4;
}
