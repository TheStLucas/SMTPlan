/**
 * The structs in this file are used to the setup of an SMTPlan instance.
 */
#ifndef KCL_options
#define KCL_options

#include <string>

namespace SMTPlan
{

	struct Argument
	{
		std::string name;
		bool has_value;
		std::string help;
	};

	struct PlannerOptions
	{
		// files
		std::string domain_path;
		std::string problem_path;
		std::string encoding_path;

		// encoding options
		bool explanatory_var_names;

		// solving options
		bool solve;

		// iterative deepening
		int lower_bound;
		int upper_bound;
		int step_size;
	};

// close namespace
}
#endif