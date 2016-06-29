/**
 * Ravi Gaddipati
 * June 22, 2016
 * rgaddip1@jhu.edu
 *
 * @brief
 * Abstract class for objects that can be used as a
 * source of reads (i.e. ReadSim and ReadFile).
 *
 * @file readsource.h
 */

#ifndef VARGAS_READS_H
#define VARGAS_READS_H

#include <string>
#include <sstream>
#include "doctest.h"
#include "utils.h"
#include "simdpp/simd.h"
#include "gdef.h"

namespace vargas {

  // Tags defining meta information in FASTA read names
  const std::string READ_META_END = "pos";
  const std::string READ_META_MUT = "sub";
  const std::string READ_META_INDEL = "ind";
  const std::string READ_META_VARNODE = "vnd";
  const std::string READ_META_VARBASE = "vbs";
  const std::string READ_META_SRC = "src";
  const char READ_FASTA_META_DELIM = ';';

/**
 * @brief
 * Struct to represent a Read.
 */
  struct Read {
      Read() : read_orig(""), read(""),
               end_pos(-1), indiv(-1), sub_err(-1), var_nodes(-1), var_bases(-1), indel_err(-1) { }
      Read(std::string r) : read_orig(""), read(r), read_num(seq_to_num(r)),
                            end_pos(-1), indiv(-1), sub_err(-1), var_nodes(-1), var_bases(-1), indel_err(-1) { }

      std::string read_orig;
      /**< unmutated read sequence */
      std::string read;
      /**< base sequence. */
      std::vector<Base> read_num;
      /**< Numeric read representation */
      int32_t end_pos;
      /**< position of last base in seq. */
      int32_t indiv;
      /**< Individual the read was taken from. */
      int32_t sub_err;
      /**< Number of substitiution errors introduced. */
      int32_t var_nodes;
      /**< Number of variant nodes the read traverses. */
      int32_t var_bases;
      /**< Number of bases that are in variant nodes. */
      int32_t indel_err;
      /**< Number of insertions and deletions introduced. */
      GID src; /**< Read origin graph, as defined in GDEF file. */

  };

  /**
   * @brief
   * Output two lines in FASTA format.
   * @details
   * Output two lines given the form: \n
   * > Meta information \n
   * read_sequence \n
   * @param r Read to print
   * @return two-line string
   */
  inline std::string to_fasta(const Read &r) {
      std::stringstream ss;
      ss << ">"
          << READ_META_END << ":" << r.end_pos << READ_FASTA_META_DELIM
          << READ_META_MUT << ":" << r.sub_err << READ_FASTA_META_DELIM
          << READ_META_INDEL << ":" << r.indel_err << READ_FASTA_META_DELIM
          << READ_META_VARNODE << ":" << r.var_nodes << READ_FASTA_META_DELIM
          << READ_META_VARBASE << ":" << r.var_bases << READ_FASTA_META_DELIM
          << READ_META_SRC << ":" << r.src
          << std::endl
          << r.read;
      return ss.str();
  }

  /**
   * @brief
   * Convert the read to a single line CSV.
   * @details
   * Output form: \n
   * src,read_seq,end_pos,sub_err,indel_err,var_nodes,var_bases \n
   * @param r Read to print
   * @return single line string
   */
  inline std::string to_csv(const Read &r) {
      std::stringstream ss;
      ss << r.src << ','
          << r.read << ','
          << r.end_pos << ','
          << r.sub_err << ','
          << r.indel_err << ','
          << r.var_nodes << ','
          << r.var_bases;
      return ss.str();
  }

  /**
   * Output the FASTA form of the read.
   * @param os output stream
   * @param r Read to output
   * @return output stream
   */
  inline std::ostream &operator<<(std::ostream &os, const Read &r) {
      os << to_fasta(r);
      return os;
  }

/**
 * @brief
 * Class defining functions for read sources.
 * @details
 * A read source encapsulates
 * one read at a time. The stored read is updated with update_read(), and obtained
 * with get_read().
 */
  class ReadSource {

    public:

      ReadSource() { }
      virtual ~ReadSource() { }

      /**
       * @brief
       * Updates the stored and and returns the read.
       * @return String representation of Read, FASTA form
       */
      virtual std::string update_and_get() {
          if (!update_read()) {
              read.read = "";
          }
          return to_string();
      }

      /**
       * @brief
       * Convert the read to the FASTA form.
       * @return two line string
       */
      virtual std::string to_string() {
          std::stringstream ss;
          Read r = get_read();
          ss << r;
          return ss.str();
      };

      /**
       * @return current Read
       */
      Read &get_read() { return read; };

      /**
       * @return all comment lines encountered
       */
      virtual std::string get_header() const = 0;

      /**
       * @brief
       * Update the current stored read.
       * @return true on success
       */
      virtual bool update_read() = 0;

      /**
       * @brief
       * Get size reads. If more reads are not available, a undersized
       * batch is returned.
       * @param size nominal number of reads to get.
       */
      const std::vector<Read> &get_batch(int size) {
          if (size <= 0) size = 1;
          _batch.clear();
          for (int i = 0; i < size; ++i) {
              if (!update_read()) break;
              _batch.push_back(read);
          }
          return _batch;
      }

      /**
       * @brief
       * Get the stored batch of reads.
       * @return vector of Reads
       */
      const std::vector<Read> &batch() const {
          return _batch;
      }

      virtual inline std::ostream &operator<<(std::ostream &os) {
          os << update_and_get();
          return os;
      }


    protected:
      Read read;
      std::string header = "";
      std::vector<Read> _batch;

  };

  /**
   * @brief
   * Container for a packaged batch of reads.
   * @details
   * Reads are interleaved so each SIMD vector
   * contains bases from all reads, respective to the base number. For example ReadBatch[0]
   * would contain the first bases of every read. Short reads or missing reads are padded
   * with Base::N.
   * @tparam num_reads max number of reads. If a non-default T is used, this should be set to
   *    SIMDPP_FAST_T_SIZE where T corresponds to the width of T. For ex. Default T=simdpp::uint8 uses
   *    SIMDPP_FAST_INT8_SIZE
   * @tparam T element type
   */

  template<unsigned int num_reads = SIMDPP_FAST_INT8_SIZE,
      template<unsigned int, typename=void> class T=simdpp::uint8>
  class ReadBatch {
    public:

      /**
       * @param len maximum read length
       */
      ReadBatch(int len) : read_len(len) { }

      /**
       * @brief
       * Read length is set to first read size.
       * @param batch package the given vector of reads. Must be nonempty.
       */
      ReadBatch(const std::vector<Read> &batch) : _reads(batch) {
          if (batch.size() == 0) throw std::invalid_argument("Vector of reads must be non-empty.");
          read_len = batch[0].read.length();
          _package_reads();
      }

      /**
       * @brief
       * Read length is set to first read size.
       * @param rs obtain a batch of reads from the Read source and package them.
       */
      ReadBatch(ReadSource &rs) : _reads(rs.get_batch(num_reads)) {
          if (_reads.size() == 0) throw std::invalid_argument("Unable to get reads.");
          read_len = _reads[0].read.length();
          _package_reads();
      }

      /**
       * @param batch package the given vector of reads. Must be nonempty.
       * @param len max read length
       */
      ReadBatch(const std::vector<Read> &batch, int len) : read_len(len), _reads(batch) {
          _package_reads();
      }

      /**
       * @brief
       * obtain a batch of reads from the Read source and package them.
       * @param rs ReadSource to pull reads from
       * @param len max read length
       */
      ReadBatch(ReadSource &rs, int len) : _reads(rs.get_batch(num_reads)), read_len(len) {
          _package_reads();
      }

      /**
       * @brief
       * Load reads from a read source.
       * @param rs Read source to load a batch from.
       */
      bool load_reads(ReadSource &rs) {
          return load_reads(rs.get_batch(num_reads));
      }

      /**
       * @param batch load the given vector of reads.
       */
      bool load_reads(const std::vector<Read> &batch) {
          if (batch.size() == 0) return false;
          _reads = batch;
          _package_reads();
          return true;
      }

      /**
       * @brief
       * Return the i'th base of every read in a simdpp vector.
       * @param i base index.
       */
      const T<num_reads> &at(int i) const {
          // let vector handle out of range errors
          return _packaged_reads.at(i);
      }

      /**
       * @brief
       * Pointer to raw packaged read data.
       * @return T<num_reads> pointer
       */
      const T<num_reads> *data() const {
          return _packaged_reads.data();
      }

      /**
       * @brief
       * Non const version of at(i).
       * @param i base index
       */
      T<num_reads> &operator[](int i) {
          return _packaged_reads.at(i);
      }

      /**
       * @return max read length. Echos template arg
       */
      size_t max_len() const { return read_len; }

      /**
       * @brief
       * Returns optimal number of reads in a batch based on SIMD architecture.
       * @return batch size.
       */
      size_t batch_size() const { return num_reads; }

      /**
       * @return Reads used to build the batch.
       */
      const std::vector<Read> &reads() const { return _reads; }

      /**
       * @brief
       * Get a read, empty read if out of range.
       * @param i index of read
       * @return Read at i
       */
      Read get_read(int i) const {
          if (i < 0 || i >= _reads.size()) return Read();
          return _reads[i];
      }

      /**
       * @brief
       * Get the utilization of the batch capacity. In effect how much
       * padding was used.
       * @return fill, between 0 and 1.
       */
      float fill() const {
          float f = 0;
          for (auto &r : _reads) f += r.read_num.size();
          return f / (num_reads * read_len);
      }

      typename std::vector<T<num_reads>>::const_iterator begin() const { return _packaged_reads.begin(); }
      typename std::vector<T<num_reads>>::const_iterator end() const { return _packaged_reads.end(); }

    private:

      int read_len;

      /**
       * _packaged_reads[i] contains all i'th bases.
       * The length of _packaged_reads is the length of the read,
       * where as the length of _packaged_reads[i] is the number
       * of reads.
       */
      std::vector<T<num_reads>> _packaged_reads;

      // Unpackaged reads
      std::vector<Read> _reads;

      /**
       * Interleaves reads so all same-index base positions are in one
       * vector. Empty spaces are padded with Base::N.
       */
      inline void _package_reads() {
          _packaged_reads.resize(read_len);
          if (_reads.size() > num_reads) throw std::range_error("Too many reads for batch size.");

          // allocate memory
          uchar **pckg = (uchar **) malloc(read_len * sizeof(uchar *));
          for (int i = 0; i < read_len; ++i) {
              pckg[i] = (uchar *) malloc(num_reads * sizeof(uchar));
          }

          // Interleave reads
          // For each read (read[i] is in _packaged_reads[0..n][i]
          for (size_t r = 0; r < _reads.size(); ++r) {

              if (_reads.at(r).read_num.size() > read_len) throw std::range_error("Read too long for batch size.");

              // Put each base in the appropriate vector element
              for (size_t p = 0; p < _reads[r].read_num.size(); ++p) {
                  pckg[p][r] = _reads[r].read_num[p];
              }

              // Pad the shorter reads
              for (size_t p = _reads[r].read_num.size(); p < read_len; ++p) {
                  pckg[p][r] = Base::N;
              }
          }

          // Pad underful batches
          for (size_t r = _reads.size(); r < num_reads; ++r) {
              for (size_t p = 0; p < read_len; ++p) {
                  pckg[p][r] = Base::N;
              }
          }

          // Load into vectors
          for (int i = 0; i < read_len; ++i) {
              _packaged_reads[i] = simdpp::load(pckg[i]);
          }

          // Free memory
          for (int i = 0; i < read_len; ++i) {
              free(pckg[i]);
          }
          free(pckg);

      }

  };
}

TEST_CASE ("Read Batch") {
    std::vector<vargas::Read> reads;
    for (int i = 0; i < 15; ++i) {
        reads.push_back(vargas::Read("ACGTACGTCAGCCNNNCTAGTANCGTACTNGGCTAGAACGTACGTCAGCC"));
        }

        SUBCASE ("packaging") {
        vargas::ReadBatch<> rb(reads, 64);

            CHECK(rb.batch_size() == SIMDPP_FAST_INT8_SIZE);
            CHECK(rb.max_len() == 64);

        auto N = base_to_num('N');
        for (size_t i = 0; i < reads[0].read_num.size(); ++i) {
            auto b = rb[i];
            auto n = reads[0].read_num[i];
                CHECK(simdpp::extract<0>(b) == n);
                CHECK(simdpp::extract<1>(b) == n);
                CHECK(simdpp::extract<2>(b) == n);
                CHECK(simdpp::extract<3>(b) == n);
                CHECK(simdpp::extract<4>(b) == n);
                CHECK(simdpp::extract<5>(b) == n);
                CHECK(simdpp::extract<6>(b) == n);
                CHECK(simdpp::extract<7>(b) == n);
                CHECK(simdpp::extract<8>(b) == n);
                CHECK(simdpp::extract<9>(b) == n);
                CHECK(simdpp::extract<10>(b) == n);
                CHECK(simdpp::extract<11>(b) == n);
                CHECK(simdpp::extract<12>(b) == n);
                CHECK(simdpp::extract<13>(b) == n);
                CHECK(simdpp::extract<14>(b) == n);
                CHECK(simdpp::extract<15>(b) == N); // Since only 15 reads in the batch
        }

        // since read_len = 64 but len(read) is 50.
        for (size_t i = reads[0].read_num.size(); i < 64; ++i) {
            auto b = rb[i];
                CHECK(simdpp::extract<0>(b) == N);
                CHECK(simdpp::extract<1>(b) == N);
                CHECK(simdpp::extract<2>(b) == N);
                CHECK(simdpp::extract<3>(b) == N);
                CHECK(simdpp::extract<4>(b) == N);
                CHECK(simdpp::extract<5>(b) == N);
                CHECK(simdpp::extract<6>(b) == N);
                CHECK(simdpp::extract<7>(b) == N);
                CHECK(simdpp::extract<8>(b) == N);
                CHECK(simdpp::extract<9>(b) == N);
                CHECK(simdpp::extract<10>(b) == N);
                CHECK(simdpp::extract<11>(b) == N);
                CHECK(simdpp::extract<12>(b) == N);
                CHECK(simdpp::extract<13>(b) == N);
                CHECK(simdpp::extract<14>(b) == N);
                CHECK(simdpp::extract<15>(b) == N);
        }
    }

    std::remove("rds_tc.reads");

}

#endif //VARGAS_READS_H
