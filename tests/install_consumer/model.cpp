#include <serene/outputs/model.hpp>
#include <array>
int main(){
  std::array<double,6> source{1,2,3,4,5,6};
  auto view=serene::outputs::ValueView::strided(source,2,3,1,2);
  if(!view)return 1;
  std::array<double,3> result{};
  if(!view->fill(1,0,result))return 2;
  return result==std::array<double,3>{2,4,6}?0:3;
}
