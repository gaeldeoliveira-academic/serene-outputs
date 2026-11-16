#include <serene/outputs/model.hpp>
#include <array>
/** Exercise the installed model component's strided gather; return zero only when the second channel is reconstructed correctly. */
int main(){
    // Bind a sample-major source and gather one logical channel into an owned destination.

  /* Sample-major source with two channels and three records. */ std::array<double,6> source{1,2,3,4,5,6};
  /* Borrowed strided logical matrix. */ auto view=serene::outputs::ValueView::strided(source,2,3,1,2);
  if(!view)return 1;
  /* Destination for the complete second channel. */ std::array<double,3> result{};
  if(!view->fill(1,0,result))return 2;
  return result==std::array<double,3>{2,4,6}?0:3;
}
