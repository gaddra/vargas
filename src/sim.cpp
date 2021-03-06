/**
 * @author Ravi Gaddipati
 * @date June 26, 2016
 * rgaddip1@jhu.edu
 *
 * @brief
 * Simulates random reads from a graph, returning reads that follow a specified Sim::Profile.
 *
 * @copyright
 * Distributed under the MIT Software License.
 * See accompanying LICENSE or https://opensource.org/licenses/MIT
 *
 * @file
 */

#include "sim.h"

bool vargas::Sim::_update_read(const coordinate_resolver& resolver) {

    uint32_t curr_indiv = 0, curr_node;
    const bool has_pop = _graph.pop_size() != 0;

    // Pick an individual

    if (has_pop) {
        do {
            curr_indiv = rand() % _graph.pop_size();
        } while (!_graph.filter()[curr_indiv]);
    }


    // Pick random weighted node and position within the node
    do {
        curr_node = _random_node_id();
    } while (has_pop && !_nodes.at(curr_node).belongs(curr_indiv));
    rg::pos_t curr_pos = rand() % _nodes.at(curr_node).length();


    int var_bases = 0;
    int var_nodes = 0;
    std::string read_str;

    while (true) {
        // Extract len subseq
        unsigned len = _prof.len - read_str.length();
        if (len > _nodes.at(curr_node).length() - curr_pos) len = _nodes.at(curr_node).length() - curr_pos;
        read_str += _nodes.at(curr_node).seq_str().substr(curr_pos, len);
        curr_pos += len;

        if (!_nodes.at(curr_node).is_ref()) {
            ++var_nodes;
            var_bases += len;
        }

        assert(read_str.length() <= _prof.len);
        if (read_str.length() == _prof.len) break; // Done

        // Pick random next node.
        if (_next.find(curr_node) == _next.end()) return false; // End of graph

        std::vector<uint32_t> valid_next;
        if (!has_pop) valid_next = _next.at(curr_node);
        else {
            for (const uint32_t n : _next.at(curr_node)) {
                if (_nodes.at(n).belongs(curr_indiv)) valid_next.push_back(n);
            }
        }
        if (valid_next.empty()) return false;
        curr_node = valid_next[rand() % valid_next.size()];
        curr_pos = 0;
    }

    if (std::count(read_str.begin(), read_str.end(), 'N') >= _prof.len / 2) return false;
    if (_prof.var_nodes >= 0 && var_nodes != _prof.var_nodes) return false;
    if (_prof.var_bases >= 0 && var_bases != _prof.var_bases) return false;

    // Introduce errors
    int sub_err = 0;
    int indel_err = 0;
    std::string read_mut;

    // Rate based errors
    if (_prof.rand) {
        for (char i : read_str) {
            char m = i;
            // Mutation error
            if (rand() % 10000 < 10000 * _prof.mut) {
                do {
                    m = rg::rand_base();
                } while (m == i);
                ++sub_err;
            }

                // Insertion
            else if (rand() % 10000 < 5000 * _prof.indel) {
                read_mut += rg::rand_base();
                ++indel_err;
            }

                // Deletion (if we don't enter)
            else if (rand() % 10000 > 5000 * _prof.indel) {
                read_mut += m;
                ++indel_err;
            }
        }
    }
    else {
        // Fixed number of errors
        sub_err = (int) std::round(_prof.mut);
        indel_err = (int) std::round(_prof.indel);
        std::set<unsigned> mut_sites;
        std::set<unsigned> indel_sites;
        read_mut = read_str;
        {
            unsigned loc;
            for (int j = 0; j < sub_err; ++j) {
                do {
                    loc = rand() % read_mut.length();
                } while (mut_sites.count(loc));
                mut_sites.insert(loc);
            }
            for (int i = 0; i < indel_err; ++i) {
                do {
                    loc = rand() % read_mut.length();
                } while (indel_sites.count(loc) || mut_sites.count(loc));
                indel_sites.insert(loc);
            }
        }

        for (unsigned m : mut_sites) {
            do {
                read_mut[m] = rg::rand_base();
            } while (read_mut[m] == read_str[m]);
        }

        for (unsigned i : indel_sites) {
            if (rand() % 2) {
                // Insertion
                read_mut.insert(i, 1, rg::rand_base());
            } else {
                // Deletion
                read_mut.erase(i, 1);
            }
        }
    }

    _read = SAM::Record();
    _read.flag.unmapped = false;
    _read.flag.aligned = true;

    _read.seq = read_mut;
    if (has_pop) _read.aux.set(SIM_SAM_INDIV_TAG, (int) curr_indiv);
    _read.aux.set(SIM_SAM_INDEL_ERR_TAG, indel_err);
    _read.aux.set(SIM_SAM_VAR_BASE_TAG, var_bases);
    _read.aux.set(SIM_SAM_VAR_NODES_TAG, var_nodes);
    _read.aux.set(SIM_SAM_SUB_ERR_TAG, sub_err);

    // +1 from length being 1 indexed but end() being zero indexed, +1 since POS is 1 indexed.
    auto resolved = resolver.resolve(_nodes.at(curr_node).end_pos() - _nodes.at(curr_node).length() + 2 + curr_pos - _prof.len);
    _read.pos = resolved.second;
    if (!resolved.first.empty()) _read.ref_name = resolved.first;

    _read.aux.set(SIM_SAM_READ_ORIG_TAG, read_str);

    return true;
}
bool vargas::Sim::update_read(const coordinate_resolver& resolver) {
    // Call internal function. update_read is a wrapper to prevent stack overflow
    unsigned counter = 0;
    while (!_update_read(resolver)) {
        ++counter;
        if (counter == _abort_after) {
            std::cerr << "Failed to generate read after " << _abort_after
                      << " tries.\n" << "Profile: " << _prof.to_string() << std::endl;
            return false;
        }
    }
    return true;
}
const std::vector<vargas::SAM::Record> &vargas::Sim::get_batch(unsigned size, const coordinate_resolver& resolver) {
    _batch.clear();
    if (size == 0) return _batch;
    for (unsigned i = 0; i < size; ++i) {
        if (!update_read(resolver)) break;
        _batch.push_back(_read);
    }
    return _batch;
}

void vargas::Sim::_init() {
    uint64_t total = 0;
    for (auto giter = _graph.begin(); giter != _graph.end(); ++giter) {
        total += giter->length();
        _node_weights.push_back(total);
        _node_ids.push_back(giter->id());
    }
    std::random_device rd;
    _rand_generator = std::mt19937(rd());
    _node_weight_dist = std::uniform_int_distribution<uint64_t>(0, total);
}


std::string vargas::Sim::Profile::to_string() const {
    std::ostringstream os;
    os << "len=" << len
       << ";mut=" << mut
       << ";indel=" << indel
       << ";vnode=" << var_nodes
       << ";vbase=" << var_bases
       << ";rand=" << rand;
    return os.str();
}

TEST_SUITE("Read Simulator");

TEST_CASE ("Read sim") {
    srand(1);
    vargas::Graph::Node::_newID = 0;
    using std::endl;
    std::string tmpfa = "tmp_tc.fa";
    {
        std::ofstream fao(tmpfa);
        fao
        << ">x" << endl
        << "CAAATAAGGCTTGGAAATTTTCTGGAGTTCTATTATATTCCAACTCTCTGGTTCCTGGTGCTATGTGTAACTAGTAATGG" << endl
        << "TAATGGATATGTTGGGCTTTTTTCTTTGATTTATTTGAAGTGACGTTTGACAATCTATCACTAGGGGTAATGTGGGGAAA" << endl
        << "TGGAAAGAATACAAGATTTGGAGCCAGACAAATCTGGGTTCAAATCCTCACTTTGCCACATATTAGCCATGTGACTTTGA" << endl
        << "ACAAGTTAGTTAATCTCTCTGAACTTCAGTTTAATTATCTCTAATATGGAGATGATACTACTGACAGCAGAGGTTTGCTG" << endl
        << "TGAAGATTAAATTAGGTGATGCTTGTAAAGCTCAGGGAATAGTGCCTGGCATAGAGGAAAGCCTCTGACAACTGGTAGTT" << endl
        << "ACTGTTATTTACTATGAATCCTCACCTTCCTTGACTTCTTGAAACATTTGGCTATTGACCTCTTTCCTCCTTGAGGCTCT" << endl
        << "TCTGGCTTTTCATTGTCAACACAGTCAACGCTCAATACAAGGGACATTAGGATTGGCAGTAGCTCAGAGATCTCTCTGCT" << endl
        << ">y" << endl
        << "GGAGCCAGACAAATCTGGGTTCAAATCCTGGAGCCAGACAAATCTGGGTTCAAATCCTGGAGCCAGACAAATCTGGGTTC" << endl;
    }
    std::string tmpvcf = "tmp_tc.vcf";

    {
        std::ofstream vcfo(tmpvcf);
        vcfo
        << "##fileformat=VCFv4.1" << endl
        << "##phasing=true" << endl
        << "##contig=<ID=x>" << endl
        << "##contig=<ID=y>" << endl
        << "##FORMAT=<ID=GT,Number=1,Type=String,Description=\"Genotype\">" << endl
        << "##INFO=<ID=AF,Number=1,Type=Float,Description=\"Allele Freq\">" << endl
        << "##INFO=<ID=AC,Number=A,Type=Integer,Description=\"Alternate Allele count\">" << endl
        << "##INFO=<ID=NS,Number=1,Type=Integer,Description=\"Num samples at site\">" << endl
        << "##INFO=<ID=NA,Number=1,Type=Integer,Description=\"Num alt alleles\">" << endl
        << "##INFO=<ID=LEN,Number=A,Type=Integer,Description=\"Length of each alt\">" << endl
        << "##INFO=<ID=TYPE,Number=A,Type=String,Description=\"type of variant\">" << endl
        << "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\ts1\ts2" << endl
        << "x\t9\t.\tG\tA,CC,T\t99\t.\tAF=0.01,0.6,0.1;AC=1;LEN=1;NA=1;NS=1;TYPE=snp\tGT\t0|1\t2|3" << endl
        << "x\t10\t.\tC\t<CN7>,<CN0>\t99\t.\tAF=0.01,0.01;AC=2;LEN=1;NA=1;NS=1;TYPE=snp\tGT\t1|1\t2|1" << endl
        << "x\t14\t.\tG\t<DUP>,<BLAH>\t99\t.\tAF=0.01,0.1;AC=1;LEN=1;NA=1;NS=1;TYPE=snp\tGT\t1|0\t1|1" << endl
        << "y\t34\t.\tTATA\t<CN2>,<CN0>\t99\t.\tAF=0.01,0.1;AC=2;LEN=1;NA=1;NS=1;TYPE=snp\tGT\t1|1\t2|1" << endl
        << "y\t39\t.\tT\t<CN0>\t99\t.\tAF=0.01;AC=1;LEN=1;NA=1;NS=1;TYPE=snp\tGT\t1|0\t0|1" << endl;
    }

    vargas::GraphFactory gb(tmpfa);
    gb.open_vcf(tmpvcf);
    gb.set_region("x:0-50");
    vargas::Graph g = gb.build();

    vargas::Sim sim(g);
    vargas::Sim::Profile prof;
    prof.len = 5;

    sim.set_prof(prof);
    CHECK(sim.update_read(vargas::coordinate_resolver()));
    {
        auto rec = sim.get_read();
        CHECK(rec.seq.length() == 5);
    }
    {
        auto rec = sim.get_batch(10, vargas::coordinate_resolver());
        CHECK(rec.size() == 10);
        for (const auto &r : rec) {
            CHECK(r.seq.length() == 5);
        }
    }

    remove(tmpfa.c_str());
    remove((tmpfa + ".fai").c_str());
    remove(tmpvcf.c_str());
}

TEST_SUITE_END();