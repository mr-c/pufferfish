#include <fstream>
#include <iostream>

#include "cereal/archives/json.hpp"
#include "cereal/archives/binary.hpp"
#include "CLI/Timer.hpp"

#include "PufferfishIndex.hpp"
#include "PufferFS.hpp"

PufferfishIndex::PufferfishIndex() {}

PufferfishIndex::PufferfishIndex(const std::string& indexDir) {
  if (!puffer::fs::DirExists(indexDir.c_str())) {
    std::cerr << "The index directory " << indexDir << " does not exist!\n";
    std::exit(1);
  }

  {
    std::ifstream infoStream(indexDir + "/info.json");
    cereal::JSONInputArchive infoArchive(infoStream);
    infoArchive(cereal::make_nvp("k", k_));
    infoArchive(cereal::make_nvp("num_kmers", numKmers_));
    std::cerr << "k = " << k_ << '\n';
    std::cerr << "num kmers = " << numKmers_ << '\n';
    infoStream.close();
  }

  //std::cerr << "loading contig table ... ";
  {
    CLI::AutoTimer timer {"Loading contig table", CLI::Timer::Big};
    std::ifstream contigTableStream(indexDir + "/ctable.bin");
    cereal::BinaryInputArchive contigTableArchive(contigTableStream);
    contigTableArchive(refNames_);
    contigTableArchive(contigTable_);
    contigTableStream.close();
  }
  //std::cerr << "done\n";

  {
    CLI::AutoTimer timer {"Loading mphf table", CLI::Timer::Big};
    std::string hfile = indexDir + "/mphf.bin";
    std::ifstream hstream(hfile);
    hash_.reset(new boophf_t);
    hash_->load(hstream);
    hstream.close();
  }

  {
    CLI::AutoTimer timer {"Loading contig boundaries", CLI::Timer::Big};
    std::string bfile = indexDir + "/rank.bin";
    sdsl::load_from_file(contigBoundary_, bfile);
    contigRank_ = decltype(contigBoundary_)::rank_1_type(&contigBoundary_);
    contigSelect_ = decltype(contigBoundary_)::select_1_type(&contigBoundary_);
  }

  {
    CLI::AutoTimer timer {"Loading sequence", CLI::Timer::Big};
    std::string sfile = indexDir + "/seq.bin";
    sdsl::load_from_file(seq_, sfile);
  }

  {
    CLI::AutoTimer timer {"Loading positions", CLI::Timer::Big};
    std::string pfile = indexDir + "/pos.bin";
    sdsl::load_from_file(pos_, pfile);
  }
}

//auto endContigMap() -> decltype(contigTable_.begin()) { return contigTable_.end(); }
uint64_t PufferfishIndex::getRawPos(CanonicalKmer& mer)  {
  auto km = mer.getCanonicalWord();
  size_t res = hash_->lookup(km);
  uint64_t pos =
    (res < numKmers_) ? pos_[res] : std::numeric_limits<uint64_t>::max();
  if (pos <= seq_.size() - k_) {
    uint64_t fk = seq_.get_int(2 * pos, 2 * k_);
    my_mer fkm;
    fkm.word__(0) = fk;
    if (mer.fwWord() == fkm.word(0) or mer.rcWord() == fkm.word(0)) {
      return pos;
    }
  }
  pos = std::numeric_limits<uint64_t>::max();
  return pos;
}

bool PufferfishIndex::contains(CanonicalKmer& mer) {
  return isValidPos(getRawPos(mer));
}
  /*
uint64_t PufferfishIndex::getRawPos(CanonicalKmer& mer)  {
  return getRawPos(mer.getCanonicalWord());
}
  */

bool PufferfishIndex::isValidPos(uint64_t pos) {
  return pos != std::numeric_limits<uint64_t>::max();
}

auto PufferfishIndex::getRefPos(CanonicalKmer& mer) -> util::ProjectedHits {

  using IterT = std::vector<util::Position>::iterator;//decltype(contigTable_.begin());
  auto km = mer.getCanonicalWord();
  size_t res = hash_->lookup(km);
  uint64_t pos =
    (res < numKmers_) ? pos_[res] : std::numeric_limits<uint64_t>::max();
  if (pos <= seq_.size() - k_) {
    uint64_t fk = seq_.get_int(2 * pos, 2 * k_);
    my_mer fkm;
    fkm.word__(0) = fk;
    bool hitFW = mer.fwWord() == fkm.word(0);
    bool hitRC = mer.rcWord() == fkm.word(0);
    if (hitFW or hitRC) {
      auto rank = contigRank_(pos);
      auto& pvec = contigTable_[rank];
      // start position of this contig
      uint64_t sp = static_cast<uint64_t>(contigSelect_(rank));
      uint32_t relPos = static_cast<uint32_t>(pos - sp);
      //auto clen = (uint64_t)realSelect(rank + 1) - sp;
      return {relPos, hitFW, core::range<IterT>{pvec.begin(), pvec.end()}};
    } else {
      return {std::numeric_limits<uint32_t>::max(), true, core::range<IterT>{}};
    }
  }
  return {std::numeric_limits<uint32_t>::max(), true, core::range<IterT>{}};
  //return core::range<IterT>{};
}

const uint32_t PufferfishIndex::k() { return k_; }

/**
 * Return the position list (ref_id, pos) corresponding to a contig.
 */
const std::vector<util::Position>& PufferfishIndex::refList(uint64_t contigRank) {
  return contigTable_[contigRank];
}

const std::string& PufferfishIndex::refName(uint64_t refRank) {
  return refNames_[refRank];
}
