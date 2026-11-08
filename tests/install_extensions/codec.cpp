#include <serene/outputs/codec.hpp>
#include <array>
int main(){
  using namespace serene::outputs;
  auto profile=CodecProfile::fixed_accuracy(1e-4);auto codec=Codec::create(32);if(!profile||!codec)return 1;
  std::array<double,32> values{},scratch{},restored{};for(unsigned i=0;i<values.size();++i)values[i]=.123456789*i+.3333333333333333;
  auto frame=codec->encode(values,scratch,*profile);if(!frame||frame->verified_count!=values.size())return 2;
  if(!codec->decode(frame->encoding,values.size(),frame->bytes,restored,*profile))return 3;
  auto verified=Codec::verify_values(values,restored,*profile);return verified?0:4;
}
