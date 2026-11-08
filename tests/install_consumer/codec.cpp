#include <serene/outputs/codec.hpp>
#include <array>
#include <cstring>
int main(){
  std::array<double,4> original{0.,0.,0.,0.},scratch{},restored{};
  auto encoder=serene::outputs::Codec::create(4);auto decoder=serene::outputs::Codec::create(4);
  if(!encoder||!decoder)return 1;
  auto frame=encoder->encode(original,scratch);if(!frame)return 2;
  if(!decoder->decode(frame->encoding,4,frame->bytes,restored))return 3;
  return std::memcmp(original.data(),restored.data(),sizeof(original))==0?0:4;
}
