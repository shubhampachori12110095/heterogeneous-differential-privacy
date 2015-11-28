// https://github.com/faithandbrave/Shand/blob/master/shand/static_map.hpp
// http://www.boost.org/doc/libs/1_57_0/doc/html/boost/container/flat_map.html
#include "args.hh"
#include "dataset.hh"
#include "abstract_user.hh"
#include "filesystem.hh"
#include "log.hh"
#include <boost/range/counting_range.hpp>
#include <boost/accumulators/statistics/sum.hpp>
#include <boost/accumulators/statistics/sum_kahan.hpp>
#include <boost/accumulators/accumulators.hpp>
#include <boost/range/algorithm/for_each.hpp>
#include <cassert>
#include <typeinfo>
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <functional>
#include <iterator>
std::string HEADER{"dataset, seed, user-id, user-class, expr, slices, min, unconcerned, normal, concerned, epsilon, recall"};

namespace gossple {
	// these must be public so as to no be destructed (and hence, closed),
	// before the application termiantes.
	std::ofstream out, log;
	std::ifstream in;
	bool out_redirected = false, log_redirected = false, in_redirected = false;

	void redirect(std::string option_name, std::ofstream &fstream, std::ostream &outstream) {
		auto fname = vm[option_name].as<std::string>();
		fs::path output{fname};
		if(fs::exists(output))
		{
			if(vm["append"].as<bool>())
				fstream = std::ofstream{fname, std::ios::app}; // append to file
			else
			{
				if(!vm["overwrite"].as<bool>()) {
					std::ostringstream cout;
					cout << "File \"" << fname << "\" already exists, but --overwrite has not been set. "
						"Cannot produce output. Quitting..." << std::ends;
					logger{"redirect"}.log(cout.str());
					exit(1);
				}
				if(!vm["header"].as<bool>() && std::addressof(fstream) == std::addressof(out))
					fstream << HEADER << std::endl;
				fstream = std::ofstream{fname, std::ios::trunc}; // overwrite file
			}
		} else {
			fstream = std::ofstream{fname}; // create file
			if(!vm["header"].as<bool>() && std::addressof(fstream) == std::addressof(out))
				fstream << HEADER << std::endl;
		}
		assert(fstream);
		outstream.rdbuf(fstream.rdbuf());
	}
}

all_t all_peers;
namespace ba = boost::accumulators;

template<typename T>
void print_if_not(std::string const & name, T const & v) {
	if(!vm[name].as<bool>())
		std::cout << v << ", ";
}

void print_if(std::string const & name) {
	if(vm[name].as<bool>())
		std::cout << name << ", ";
}

template<typename T>
void print_or_na_or_either(std::string const & expr1, std::string const & expr2, std::string const & name, T const & v) {
	if(vm.count(name) or vm[expr1].as<bool>() or vm[expr2].as<bool>())
		std::cout << v << ", ";
	else
		std::cout << "NA" << ", ";
}

template<typename T>
void print_or_na(std::string const & name, T const & v) {
	if(vm.count(name))
		std::cout << v << ", ";
	else
		std::cout << "NA" << ", ";
}

template<typename T>
void print_or_na(std::string const & name) {
	if(vm.count(name))
		std::cout << vm[name].as<T>() << ", ";
	else
		std::cout << "NA" << ", ";
}

template<typename T>
void print_or_ignore(std::string const & name) {
	if(vm.count(name))
		std::cout << vm[name].as<T>() << ", ";
}

uint32_t current_cycle = 0;
int main(int argc, char *argv[]) {
	parse_args(argc, argv);

	if(vm.count("output")) { gossple::redirect("output", gossple::out, std::cout); gossple::out_redirected = true; }
	if(vm.count("log"   )) { gossple::redirect("log"   , gossple::log, std::clog); gossple::log_redirected = true; }

	static logger l{"main"};

	if(vm["private"].as<bool>())
	{
		assert(vm.count("epsilon"));
		// must have exactly one of the three
		assert(( vm["naive"].as<bool>() and !vm["groups"].as<bool>() and !vm.count("slices")) or
		       (!vm["naive"].as<bool>() and  vm["groups"].as<bool>() and !vm.count("slices")) or
		       (!vm["naive"].as<bool>() and !vm["groups"].as<bool>() and  vm.count("slices")));
	}
	else
		assert(!vm.count("epsilon") and !vm["naive"].as<bool>() and !vm["groups"].as<bool>() and !vm.count("slices"));

	// for reproducibility
	if(vm.count("random-seed"))
		rng = std::default_random_engine{vm["random-seed"].as<uint32_t>()};

	// TODO check paper: Push-Pull Functional Reactive Programming - Conal Elliott
	// TODO: is search (recall) done also on RPS view??

	l.log("Initializing peers");
	if(vm.count("dataset")) {
		auto fname = vm["dataset"].as<std::string>();
		if(fname != "-" /*stdin*/) {
			fs::path path{fname};
			if(!fs::exists(path)) {
				l.log("File \"%s\" does not exist. Cannot read dataset. Quitting...", fname.c_str());
				exit(1);
			}
			gossple::in = std::ifstream{fname};
			std::cin.rdbuf(gossple::in.rdbuf());
			gossple::in_redirected = true;

			l.log("Reading input dataset from \"%s\"", fname.c_str());
		} else
			l.log("Reading input dataset from stdin");
	} else
		l.log("Reading input dataset from stdin");
	auto joined_peers = load_dataset();

	l.log("Simulating cycles");

	for(auto i : boost::counting_range(0u, cycles))
	{
		current_cycle++;

		//boost::for_each(joined_peers, std::mem_fn(&user::vicinity_do_gossip));
		uint32_t j = 0;
		for(auto &v : joined_peers) {
			v->vicinity_do_gossip();
			++j;
			l.progress(current_cycle, cycles, (j%100) == 0);
		}
	}
	l.log("All cycles finished");

	//ba::accumulator_set<double, ba::features<ba::tag::sum_kahan>> acc;

	if(vm["header"].as<bool>())
		std::cout << HEADER << std::endl;
	for(auto a : joined_peers)
	{
		auto recall = a->recall();

		//l.log("recall(%d) = %f", a->id, recall);
		//acc(recall);
		print_or_na<std::string>("dataset");
		print_or_na<uint32_t>("random-seed");
		std::cout << (a->id) << ", ";
		std::cout << a->cls() << ", ";
		// randomness type, or randomness seed
		// --- experiment type
		print_if("naive");
		print_if("groups");
		print_or_ignore<uint32_t>("slices");
		print_if_not("private", "baseline");
		// --- slices info
		print_or_na<uint32_t>("slices");
		print_or_na<double>("min");
		// --- groups info
		extern double unconcerned, normal, concerned;
		print_or_na_or_either("naive", "groups", "unconcerned", unconcerned);
		print_or_na_or_either("naive", "groups", "normal", normal);
		print_or_na_or_either("naive", "groups", "concerned", concerned);
		// --- privacy options
		extern double epsilon;
		std::cout << epsilon << ", ";
		std::cout << recall << std::endl;
	}

	//auto avg_recall = ba::sum_kahan(acc) / static_cast<double>(joined_peers.size());

	//std::cout << avg_recall << std::endl ;
	return 0;
}
