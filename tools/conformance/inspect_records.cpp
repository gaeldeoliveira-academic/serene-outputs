// Independent N01 Windows conformance inspector. Not a production decoder.
#include <windows.h>
#include <bcrypt.h>
#include <zfp.h>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
using Bytes=std::vector<unsigned char>;
namespace fs=std::filesystem;
/** Require predicate p, throwing runtime_error with borrowed message m on failure. Returns void; main catches the exception. */
void require(bool p,const char* m){
  // Translate a failed inspection invariant into the CLI error boundary.
if(!p)throw std::runtime_error(m);}
/** Read trusted fixture path p into owned bytes. This tiny-sidecar inspector deliberately materializes complete inputs and is not a hostile-input transport. */
Bytes read(const fs::path& p){
  // Open the input in binary mode, then own its complete byte range.
/* Owned binary file stream for this whole-fixture read. */ std::ifstream f(p,std::ios::binary);require(bool(f),"open failed");return Bytes(std::istreambuf_iterator<char>(f),{});}
/** Decode eight little-endian bytes at borrowed p into uint64. Caller proves the eight-byte extent. */
std::uint64_t u64(const unsigned char* p){
  // Accumulate the eight byte contributions without alignment or native-struct assumptions.
/* Unsigned eight-byte accumulator. */ std::uint64_t x=0;for(/* Byte position in the fixed little-endian field. */ unsigned i=0;i<8;i++)x|=std::uint64_t(p[i])<<(i*8);return x;}
/** Decode four little-endian bytes at borrowed p into uint32. Used for the profile field; caller proves the extent. */
std::uint32_t u32(const unsigned char* p){
  // Accumulate the four byte contributions without alignment assumptions.
/* Unsigned four-byte accumulator. */ std::uint32_t x=0;for(/* Byte position in the fixed little-endian field. */ unsigned i=0;i<4;i++)x|=std::uint32_t(p[i])<<(i*8);return x;}
/** Append low n bytes of x in little-endian order to owned accumulator b; internal n is at most eight. */
void put(Bytes& b,std::uint64_t x,unsigned n){
  // Emit one low-to-high byte at a time for independent canonical-record reconstruction.
for(/* Byte position in the fixed little-endian field. */ unsigned i=0;i<n;i++)b.push_back(static_cast<unsigned char>(x>>(8*i)));}
/** Return SHA-256 of borrowed tiny bytes b using Windows BCrypt; throws on provider failure and retains no input borrow. */
std::array<unsigned char,32> digest(std::span<const unsigned char> b){
  // Hash the supplied bytes into an owned fixed-size digest.
/* Owned digest result. */ std::array<unsigned char,32> out{};require(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(b.data()),static_cast<ULONG>(b.size()),out.data(),32)>=0,"SHA256 failed");return out;}
/** Owned aggregate inspection counters shared across fixture runs. main prints these only after every selected sidecar passes. No decoded history is retained here. */
struct Summary{std::size_t runs{}, ///< Fully inspected logical runs.
frames{}, ///< Canonical frames whose bytes and values passed.
zfp{}, ///< Frames decoded through public reversible ZFP.
raw{}, ///< Frames compared directly as raw binary64 bytes.
values{}; ///< Exact binary64 values compared across all frames.
};
/** Inspect sidecars selected by shape (.shape.txt) and add successful frame/value counts to sum. Fully materializes trusted tiny sidecars; verifies canonical 77-byte records, payload digests and exact decoded bits using public ZFP. */
void inspect(const fs::path& shape,Summary& sum){
  // Load dimensions and the index/payload/expected byte oracles.
  // Walk frames in canonical order and independently repack every record.
  // Compare raw or public-ZFP decoded bytes exactly, then prove terminal coverage.

  /* Sidecar filename stem after removing .shape.txt. */ auto base=shape.string();base.resize(base.size()-10);
  /* Text shape oracle stream. */ std::ifstream sf(shape);/* Expected matrix row/inner dimensions. */ std::uint64_t outer{},inner{};sf>>outer>>inner;require(bool(sf),"shape parse");
  /* Owned canonical index, encoded payload and exact-value oracle bytes. */ auto index=read(base+".index.bin"),payload=read(base+".payload.bin"),expected=read(base+".expected.bin");
  require(index.size()%77==0,"record size");require(expected.size()==outer*inner*8,"expected shape");
  /* Independent repacking accumulator for record byte equality. */ Bytes canonical;canonical.reserve(index.size());/* Next required row, row-relative value and payload-byte positions. */ std::uint64_t next_outer=0,next_inner=0,next_byte=0;
  for(/* Current canonical frame-record ordinal. */ std::size_t n=0;n<index.size()/77;n++){
    /* Borrowed pointer to the current complete 77-byte record. */ auto p=index.data()+n*77;/* Decoded row/start/count/payload-offset/payload-size fields. */ auto row=u64(p),begin=u64(p+8),count=u64(p+16),off=u64(p+24),size=u64(p+32);/* Wire raw-or-ZFP encoding byte. */ auto enc=p[40];/* Pinned reversible profile index. */ auto profile=u32(p+41);
    require(row==next_outer&&begin==next_inner&&off==next_byte,"canonical coverage");require(count>0&&count<=4&&begin+count<=inner,"count bounds");require(off+size<=payload.size()&&profile==0,"extent/profile");
    for(/* One of the five leading u64 record fields. */ unsigned k=0;k<5;k++)put(canonical,u64(p+8*k),8);put(canonical,enc,1);put(canonical,profile,4);canonical.insert(canonical.end(),p+45,p+77);
    /* Borrowed current encoded payload slice. */ auto bytes=std::span<const unsigned char>(payload.data()+off,static_cast<std::size_t>(size));/* Independent digest of the actual payload slice. */ auto hash=digest(bytes);require(std::memcmp(hash.data(),p+45,32)==0,"payload digest");
    /* Borrowed exact-value oracle slice for this frame. */ auto gold=expected.data()+(row*inner+begin)*8;
    if(enc==0){require(size==count*8,"raw length");require(std::memcmp(bytes.data(),gold,size)==0,"raw bits");sum.raw++;}
    else{
      require(enc==1,"encoding");/* Public ZFP bitstream wrapper borrowing encoded bytes. */ auto* stream=stream_open(const_cast<unsigned char*>(bytes.data()),bytes.size());/* Public ZFP stream state, released after this frame. */ auto* z=zfp_stream_open(stream);/* Public ZFP field description, released after this frame. */ auto* field=zfp_field_alloc();
      /* Number of header bits consumed by the public decoder. */ auto header=zfp_read_header(z,field,ZFP_HEADER_FULL);require(header==96,"header bound");require(zfp_field_type(field)==zfp_type_double&&zfp_field_dimensionality(field)==1&&zfp_field_size(field,nullptr)==count,"header shape");require(zfp_stream_compression_mode(z)==zfp_mode_reversible&&zfp_stream_mode(z)==2176,"header mode");/* Public decoded minimum/maximum bits and precision parameters. */ uint mn,mx,prec;/* Public minimum exponent parameter for the pinned reversible tuple. */ int exp;zfp_stream_params(z,&mn,&mx,&prec,&exp);require(mn==1&&mx==16658&&prec==64&&exp==-1075,"mode tuple");
      /* Owned tiny decoded-frame buffer for exact bit comparison. */ std::vector<double> result(count);zfp_field_set_pointer(field,result.data());/* Bytes consumed by public decompression. */ auto consumed=zfp_decompress(z,field);require(consumed==size,"consumed bytes");require(std::memcmp(result.data(),gold,count*8)==0,"zfp bits");
      zfp_field_free(field);zfp_stream_close(z);stream_close(stream);sum.zfp++;
    }
    next_byte+=size;next_inner+=count;if(next_inner==inner){next_outer++;next_inner=0;}sum.frames++;sum.values+=count;
  }
  require(canonical==index,"canonical byte roundtrip");require(next_byte==payload.size(),"payload terminal");require((outer==0||inner==0)?index.empty():(next_outer==outer&&next_inner==0),"logical terminal");sum.runs++;
}
/** Inspect every .shape.txt under argv[1]; argc must be two. Prints aggregate JSON on success; reports caught exceptions to stderr and returns one on failure. No production decoder or HDF5 reader is used. */
int main(int argc,char** argv){
  // Check arguments, host endianness and public codec version.
  // Inspect each trusted sidecar set and print the aggregate result, translating exceptions to failure status.
try{require(argc==2,"expected fixture directory");require(std::endian::native==std::endian::little,"host endian");require(zfp_codec_version==5,"codec version");/* Whole-directory completed inspection counters. */ Summary s;for(/* Current directory entry considered for its shape sidecar suffix. */ const auto& e:fs::directory_iterator(argv[1]))if(e.path().filename().string().ends_with(".shape.txt"))inspect(e.path(),s);std::cout<<"{\"runs\":"<<s.runs<<",\"frames\":"<<s.frames<<",\"raw\":"<<s.raw<<",\"zfp\":"<<s.zfp<<",\"values\":"<<s.values<<",\"canonical_bytes\":\"pass\",\"payload_hashes\":\"pass\",\"exact_values\":\"pass\",\"scope\":\"trusted tiny independent sidecars; not production HDF5 validation\"}\n";}catch(/* Inspection error reported to stderr. */ const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
