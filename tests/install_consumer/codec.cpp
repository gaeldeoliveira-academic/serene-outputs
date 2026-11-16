#include <serene/outputs/codec.hpp>
#include <array>
#include <cstring>
/** Exercise separate installed reversible encoder/decoder owners; return zero only for a bit-exact four-value round trip. */
int main(){
    // Prepare independent codecs and scratch buffers, encode once, decode, and compare original bits.

  /* Four zero source values plus separate scratch/restored buffers. */ std::array<double,4> original{0.,0.,0.,0.},scratch{},restored{};
  /* Encoder owning the borrowed encoded frame bytes. */ auto encoder=serene::outputs::Codec::create(4);/* Independent decoder proving installed linkage. */ auto decoder=serene::outputs::Codec::create(4);
  if(!encoder||!decoder)return 1;
  /* Encoded result consumed while encoder storage is unchanged. */ auto frame=encoder->encode(original,scratch);if(!frame)return 2;
  if(!decoder->decode(frame->encoding,4,frame->bytes,restored))return 3;
  return std::memcmp(original.data(),restored.data(),sizeof(original))==0?0:4;
}
