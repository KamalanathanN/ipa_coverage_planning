#include <ipa_room_exploration/flow_network_explorator.h>

// Constructor
flowNetworkExplorator::flowNetworkExplorator()
{

}

// Function that creates a Cbc optimization problem and solves it, using the given matrices and vectors and the 3-stage
// ansatz, that takes an initial step going from the start node and then a coverage stage assuming that the number of
// flows into and out of a node must be the same. At last a final stage is gone, that terminates the path in one of the
// possible nodes.
void flowNetworkExplorator::solveThreeStageOptimizationProblem(std::vector<double>& C, const cv::Mat& V, const std::vector<double>& weights,
			const std::vector<std::vector<uint> >& flows_into_nodes, const std::vector<std::vector<uint> >& flows_out_of_nodes,
			const std::vector<uint>& start_arcs)
{
	// initialize the problem
	CoinModel problem_builder;

	std::cout << "Creating and solving linear program." << std::endl;

	// add the optimization variables to the problem
	int number_of_variables = 0;
	for(size_t arc=0; arc<start_arcs.size(); ++arc) // initial stage
	{
		problem_builder.setColBounds(number_of_variables, 0.0, 1.0);
		problem_builder.setObjective(number_of_variables, weights[start_arcs[arc]]);
		problem_builder.setInteger(number_of_variables);
		++number_of_variables;
//		}
	}
	for(size_t variable=0; variable<V.cols; ++variable) // coverage stage
	{
		problem_builder.setColBounds(number_of_variables, 0.0, 1.0);
		problem_builder.setObjective(number_of_variables, weights[variable]);
		problem_builder.setInteger(number_of_variables);
		++number_of_variables;
	}
	for(size_t variable=0; variable<V.cols; ++variable) // final stage
	{
		problem_builder.setColBounds(number_of_variables, 0.0, 1.0);
		problem_builder.setObjective(number_of_variables, weights[variable]);
		problem_builder.setInteger(number_of_variables);
		++number_of_variables;
	}
	for(size_t aux_flow=0; aux_flow<V.cols+start_arcs.size(); ++aux_flow) // auxiliary flow variables for initial and coverage stage
	{
		problem_builder.setColBounds(number_of_variables, 0.0, COIN_DBL_MAX); // auxiliary flow at least 0
		problem_builder.setObjective(number_of_variables, 0.0); // no additional part in the objective
		++number_of_variables;
	}
	for(size_t indicator=0; indicator<flows_into_nodes.size(); ++indicator) // indicator variables showing if a node is in the path
	{
		problem_builder.setColBounds(number_of_variables, 0.0, 1.0);
		problem_builder.setObjective(number_of_variables, 0.0); // no additional part in the objective, TODO: check if including number of chosen nodes in objective
		problem_builder.setInteger(number_of_variables);
		++number_of_variables;
	}

	std::cout << "number of variables in the problem: " << number_of_variables << std::endl;

	// inequality constraints to ensure that every position has been seen at least once:
	//		for each center that should be covered, find the arcs of the three stages that cover it
	for(size_t row=0; row<V.rows; ++row)
	{
		std::vector<int> variable_indices;

		// initial stage, TODO: check this for correctness
		for(size_t col=0; col<start_arcs.size(); ++col)
			if(V.at<uchar>(row, start_arcs[col])==1)
				variable_indices.push_back((int) col);

		// coverage and final stage
		for(size_t col=0; col<V.cols; ++col)
		{
			if(V.at<uchar>(row, col)==1)
			{
				variable_indices.push_back((int) col + start_arcs.size()); // coverage stage
				variable_indices.push_back((int) col + start_arcs.size() + V.cols); // final stage
			}
		}

		// all indices are 1 in this constraint
		std::vector<double> variable_coefficients(variable_indices.size(), 1.0);

		// add the constraint, if the current cell can be covered by the given arcs
		if(variable_indices.size()>0)
			problem_builder.addRow((int) variable_indices.size(), &variable_indices[0], &variable_coefficients[0], 1.0);
	}


	// equality constraint to ensure that the number of flows out of one node is the same as the number of flows into the
	// node during the coverage stage
	//	Remark: for initial stage ensure that exactly one arc is gone, because there only the outgoing flows are taken
	//			into account
	// initial stage
	std::vector<int> start_indices(start_arcs.size());
	std::vector<double> start_coefficients(start_arcs.size());
	for(size_t start=0; start<start_arcs.size(); ++start)
	{
		start_indices[start] = start;
		start_coefficients[start] = 1.0;
	}
	problem_builder.addRow((int) start_indices.size(), &start_indices[0], &start_coefficients[0], 1.0, 1.0);

	// coverage stage, also add the flow decreasing and node indicator constraints
	for(size_t node=0; node<flows_into_nodes.size(); ++node)
	{
		// vectors for the conservativity constraint
		std::vector<int> variable_indices;
		std::vector<double> variable_coefficients;

		// vectors for the decreasing equality constraint that ensures that the flow gets reduced by 1, every time it passes a node,
		// cycles are prevented by this, because a start of a cycle is also an end of it, producing a constraint that the flow
		// trough this node needs to be larger than from any other arc in this cycle but also it needs to be smaller than
		// any other flow, which is not possible
		std::vector<int> flow_decrease_indices;
		std::vector<double> flow_decrease_coefficients;

		// vectors for the node indicator equality constraints that sets the indicator for the node to 1, if an arc flows
		// into this node during the initial or coverage stage
		std::vector<int> indicator_indices;
		std::vector<double> indicator_coefficients;

		// gather flows into node
		for(size_t inflow=0; inflow<flows_into_nodes[node].size(); ++inflow)
		{
			// if a start arcs flows into the node, additionally take the index of the arc in the start_arc vector
			if(contains(start_arcs, flows_into_nodes[node][inflow])==true)
			{
				// conservativity
				variable_indices.push_back(std::find(start_arcs.begin(), start_arcs.end(), flows_into_nodes[node][inflow])-start_arcs.begin());
				variable_coefficients.push_back(1.0);
				// decreasing flow
				flow_decrease_indices.push_back(variable_indices.back() + start_arcs.size() + 2.0*V.cols);
				flow_decrease_coefficients.push_back(1.0);
				// node indicator
				indicator_indices.push_back(variable_indices.back());
				indicator_coefficients.push_back(1.0);
			}
			// get the index of the arc in the optimization vector
			// conservativity
			variable_indices.push_back(flows_into_nodes[node][inflow] + start_arcs.size());
			variable_coefficients.push_back(1.0);
			// decreasing flow
			flow_decrease_indices.push_back(variable_indices.back() + start_arcs.size() + 2.0*V.cols);
			flow_decrease_coefficients.push_back(1.0);
			// node indicator
			indicator_indices.push_back(flows_into_nodes[node][inflow] + start_arcs.size());
			indicator_coefficients.push_back(1.0);
		}

		// gather flows out of node, also include flows into final nodes (for conservativity)
		for(size_t outflow=0; outflow<flows_out_of_nodes[node].size(); ++outflow)
		{
			// coverage stage variable
			// conservativity
			variable_indices.push_back(flows_out_of_nodes[node][outflow] + start_arcs.size());
			variable_coefficients.push_back(-1.0);
			// flow decreasing
			flow_decrease_indices.push_back(flows_out_of_nodes[node][outflow] + 2.0*(start_arcs.size()+V.cols));
			flow_decrease_coefficients.push_back(-1.0);
			// final stage variable
			variable_indices.push_back(flows_out_of_nodes[node][outflow] + start_arcs.size() + V.cols);
			variable_coefficients.push_back(-1.0);
		}

//		testing
//		std::cout << "number of flows: " << variable_indices.size() << std::endl;
//		for(size_t i=0; i<variable_indices.size(); ++i)
//			std::cout << variable_indices[i] << std::endl;

		// add conservativity constraint
		problem_builder.addRow((int) variable_indices.size(), &variable_indices[0], &variable_coefficients[0], 0.0, 0.0);

		// add node indicator variable to flow decreasing constraint
		flow_decrease_indices.push_back(node + 2.0*start_arcs.size() + 3.0*V.cols);
		flow_decrease_coefficients.push_back(-1.0);

		// add flow decreasing constraint
//		std::cout << "decreasing constraint" << std::endl;
		problem_builder.addRow((int) flow_decrease_indices.size(), &flow_decrease_indices[0], &flow_decrease_coefficients[0], 0.0, 0.0);

		// get node indicator variable for the indicator constraint
//		std::cout << "indicator constraint" << std::endl;
		indicator_indices.push_back(node + 2.0*start_arcs.size() + 3.0*V.cols);
		indicator_coefficients.push_back(-1.0);

		// add node indicator constraint
		problem_builder.addRow((int) indicator_indices.size(), &indicator_indices[0], &indicator_coefficients[0], 0.0, 0.0);
	}

	// equality constraint to ensure that the path only once goes to the final stage
	std::vector<int> final_indices(V.cols);
	std::vector<double> final_coefficients(final_indices.size());
	// gather indices
	for(size_t node=0; node<final_indices.size(); ++node)
	{
		final_indices[node] = node + start_arcs.size() + V.cols;
		final_coefficients[node] = 1.0;
	}
	// add constraint
	problem_builder.addRow((int) final_indices.size(), &final_indices[0], &final_coefficients[0], 1.0, 1.0);

	// inequality constraints changing the maximal flow along an arc, if this arc is gone in the path
	std::cout << "max flow constraints" << std::endl;
	for(size_t node=0; node<V.cols+start_arcs.size(); ++node)
	{
		// size of two, because each auxiliary flow corresponds to exactly one arc indication variable
		std::vector<int> aux_flow_indices(2);
		std::vector<double> aux_flow_coefficients(2);

		// first entry shows indication variable
		aux_flow_indices[0] = node;
		aux_flow_coefficients[0] = flows_into_nodes.size()-1; // allow a high flow if the arc is chosen in the path

		// second entry shows the flow variable
		aux_flow_indices[1] = node+start_arcs.size()+2.0*V.cols;
		aux_flow_coefficients[1] = -1.0;

		// add constraint
		problem_builder.addRow((int) aux_flow_indices.size(), &aux_flow_indices[0], &aux_flow_coefficients[0], 0.0);
	}

	// equality constraints to set the flow out of the start to the number of gone nodes
	std::cout << "init flow constraints" << std::endl;
	std::vector<int> start_flow_indices(start_arcs.size()+flows_into_nodes.size());
	std::vector<double> start_flow_coefficients(start_flow_indices.size());
	for(size_t node=0; node<start_arcs.size(); ++node) // start arcs
	{
		start_flow_indices[node] = node+start_arcs.size()+2.0*V.cols;
		start_flow_coefficients[node] = 1.0;
	}
	for(size_t indicator=0; indicator<flows_into_nodes.size(); ++indicator) // node indicator variables
	{
		start_flow_indices[indicator+start_arcs.size()] = indicator+2.0*start_arcs.size()+3.0*V.cols;
		start_flow_coefficients[indicator+start_arcs.size()] = -1.0;
	}
	problem_builder.addRow((int) start_flow_indices.size(), &start_flow_indices[0], &start_flow_coefficients[0], 0.0, 0.0);

	// load the created LP problem to the solver
	OsiClpSolverInterface LP_solver;
	OsiClpSolverInterface* solver_pointer = &LP_solver;

	solver_pointer->loadFromCoinModel(problem_builder);

	// testing
	solver_pointer->writeLp("lin_flow_prog", "lp");

	// solve the created integer optimization problem
	CbcModel model(*solver_pointer);
	model.solver()->setHintParam(OsiDoReducePrint, true, OsiHintTry);

//	CbcHeuristicLocal heuristic2(model);
	CbcHeuristicFPump heuristic(model);
	model.addHeuristic(&heuristic);

	model.initialSolve();
	model.branchAndBound();

	// retrieve solution
	const double* solution = model.solver()->getColSolution();

	for(size_t res=0; res<number_of_variables; ++res)
	{
//		std::cout << solution[res] << std::endl;
		C[res] = solution[res];
	}
}

// Function that creates a Cbc optimization problem and solves it, using the given matrices and vectors and the 3-stage
// ansatz, that takes an initial step going from the start node and then a coverage stage assuming that the number of
// flows into and out of a node must be the same. At last a final stage is gone, that terminates the path in one of the
// possible nodes. This function uses lazy generalized cutset inequalities (GCI) to prevent cycles. For that a solution
// without cycle prevention constraints is determined and then cycles are detected in this solution. For these cycles
// then additional constraints are added and a new solution is determined. This procedure gets repeated until no cycle
// is detected in the solution or the only cycle contains all visited nodes, because such a solution is a traveling
// salesman like solution, which is a valid solution.
void flowNetworkExplorator::solveLazyConstraintOptimizationProblem(std::vector<double>& C, const cv::Mat& V, const std::vector<double>& weights,
		const std::vector<std::vector<uint> >& flows_into_nodes, const std::vector<std::vector<uint> >& flows_out_of_nodes,
		const std::vector<uint>& start_arcs)
{
	// initialize the problem
	CoinModel problem_builder;

	std::cout << "Creating and solving linear program." << std::endl;

	// add the optimization variables to the problem
	int number_of_variables = 0;
	for(size_t arc=0; arc<start_arcs.size(); ++arc) // initial stage
	{
		problem_builder.setColBounds(number_of_variables, 0.0, 1.0);
		problem_builder.setObjective(number_of_variables, weights[start_arcs[arc]]);
		problem_builder.setInteger(number_of_variables);
		++number_of_variables;
//		}
	}
	for(size_t variable=0; variable<V.cols; ++variable) // coverage stage
	{
		problem_builder.setColBounds(number_of_variables, 0.0, 1.0);
		problem_builder.setObjective(number_of_variables, weights[variable]);
		problem_builder.setInteger(number_of_variables);
		++number_of_variables;
	}
	for(size_t variable=0; variable<V.cols; ++variable) // final stage
	{
		problem_builder.setColBounds(number_of_variables, 0.0, 1.0);
		problem_builder.setObjective(number_of_variables, weights[variable]);
		problem_builder.setInteger(number_of_variables);
		++number_of_variables;
	}
	std::cout << "number of variables in the problem: " << number_of_variables << std::endl;

	// inequality constraints to ensure that every position has been seen at least once:
	//		for each center that should be covered, find the arcs of the three stages that cover it
	for(size_t row=0; row<V.rows; ++row)
	{
		std::vector<int> variable_indices;

		// initial stage, TODO: check this for correctness
		for(size_t col=0; col<start_arcs.size(); ++col)
			if(V.at<uchar>(row, start_arcs[col])==1)
				variable_indices.push_back((int) col);

		// coverage and final stage
		for(size_t col=0; col<V.cols; ++col)
		{
			if(V.at<uchar>(row, col)==1)
			{
				variable_indices.push_back((int) col + start_arcs.size()); // coverage stage
				variable_indices.push_back((int) col + start_arcs.size() + V.cols); // final stage
			}
		}

		// all indices are 1 in this constraint
		std::vector<double> variable_coefficients(variable_indices.size(), 1.0);

		// add the constraint, if the current cell can be covered by the given arcs
		if(variable_indices.size()>0)
			problem_builder.addRow((int) variable_indices.size(), &variable_indices[0], &variable_coefficients[0], 1.0);
	}


	// equality constraint to ensure that the number of flows out of one node is the same as the number of flows into the
	// node during the coverage stage
	//	Remark: for initial stage ensure that exactly one arc is gone, because there only the outgoing flows are taken
	//			into account
	// initial stage
	std::vector<int> start_indices(start_arcs.size());
	std::vector<double> start_coefficients(start_arcs.size());
	for(size_t start=0; start<start_arcs.size(); ++start)
	{
		start_indices[start] = start;
		start_coefficients[start] = 1.0;
	}
	problem_builder.addRow((int) start_indices.size(), &start_indices[0], &start_coefficients[0], 1.0, 1.0);

	// coverage stage
	for(size_t node=0; node<flows_into_nodes.size(); ++node)
	{
		std::vector<int> variable_indices;
		std::vector<double> variable_coefficients;

		// gather flows into node
		for(size_t inflow=0; inflow<flows_into_nodes[node].size(); ++inflow)
		{
			// if a start arcs flows into the node, additionally take the index of the arc in the start_arc vector
			if(contains(start_arcs, flows_into_nodes[node][inflow])==true)
			{
				// conservativity
				variable_indices.push_back(std::find(start_arcs.begin(), start_arcs.end(), flows_into_nodes[node][inflow])-start_arcs.begin());
				variable_coefficients.push_back(1.0);
			}
			// get the index of the arc in the optimization vector
			// conservativity
			variable_indices.push_back(flows_into_nodes[node][inflow] + start_arcs.size());
			variable_coefficients.push_back(1.0);
		}

		// gather flows out of node, also include flows into final nodes (for conservativity)
		for(size_t outflow=0; outflow<flows_out_of_nodes[node].size(); ++outflow)
		{
			// coverage stage variable
			// conservativity
			variable_indices.push_back(flows_out_of_nodes[node][outflow] + start_arcs.size());
			variable_coefficients.push_back(-1.0);
			// final stage variable
			variable_indices.push_back(flows_out_of_nodes[node][outflow] + start_arcs.size() + V.cols);
			variable_coefficients.push_back(-1.0);
		}

//		testing
//		std::cout << "number of flows: " << variable_indices.size() << std::endl;
//		for(size_t i=0; i<variable_indices.size(); ++i)
//			std::cout << variable_indices[i] << std::endl;

		// add conservativity constraint
		problem_builder.addRow((int) variable_indices.size(), &variable_indices[0], &variable_coefficients[0], 0.0, 0.0);
	}

	// equality constraint to ensure that the path only once goes to the final stage
	std::vector<int> final_indices(V.cols);
	std::vector<double> final_coefficients(final_indices.size());
	// gather indices
	for(size_t node=0; node<final_indices.size(); ++node)
	{
		final_indices[node] = node + start_arcs.size() + V.cols;
		final_coefficients[node] = 1.0;
	}
	// add constraint
	problem_builder.addRow((int) final_indices.size(), &final_indices[0], &final_coefficients[0], 1.0, 1.0);

	// load the created LP problem to the solver
	OsiClpSolverInterface LP_solver;
	OsiClpSolverInterface* solver_pointer = &LP_solver;

	solver_pointer->loadFromCoinModel(problem_builder);

	// testing
	solver_pointer->writeLp("lin_flow_prog", "lp");

	// solve the created integer optimization problem
	CbcModel model(*solver_pointer);
	model.solver()->setHintParam(OsiDoReducePrint, true, OsiHintTry);

	CbcHeuristicFPump heuristic(model);
	model.addHeuristic(&heuristic);

	model.initialSolve();
	model.branchAndBound();

//	testing
//	std::vector<int> test_row(2);
//	std::vector<double> test_coeff(2);
//
//	test_row[0] = 0;
//	test_row[1] = 1;
//
//	test_coeff[0] = 1.0;
//	test_coeff[1] = 1.0;
//	solver_pointer->addRow(2, &test_row[0], &test_coeff[0], 0.0, 0.0);
//	solver_pointer->writeLp("lin_flow_prog", "lp");
//	solver_pointer->resolve();

	// retrieve solution
	const double* solution = model.solver()->getColSolution();

	// search for cycles in the retrieved solution, if one is found add a constraint to prevent this cycle
	// TODO: finish adding and resolving lazy constraints
	bool cycle_free = false;

	do
	{
		// get the arcs that are used in the previously calculated solution
		std::set<uint> used_arcs; // set that stores the indices of the arcs corresponding to non-zero elements in the solution

		// go trough the start arcs
		for(size_t start_arc=0; start_arc<start_arcs.size(); ++start_arc)
		{
			if(solution[start_arc]!=0)
			{
				// insert start index
				used_arcs.insert(start_arcs[start_arc]);
			}
		}

		// go trough the coverage stage
		for(size_t arc=start_arcs.size(); arc<start_arcs.size()+V.cols; ++arc)
		{
			if(solution[arc]!=0)
			{
				// insert index, relative to the first coverage variable
				used_arcs.insert(arc-start_arcs.size());
			}
		}

		// go trough the final stage and find the remaining used arcs
		std::vector<std::vector<uint> > reduced_outflows(flows_out_of_nodes.size());
		for(size_t node=0; node<flows_out_of_nodes.size(); ++node)
		{
			for(size_t flow=0; flow<flows_out_of_nodes[node].size(); ++flow)
			{
				if(solution[flows_out_of_nodes[node][flow]+start_arcs.size()+V.cols]!=0)
				{
					// insert saved outgoing flow index
					used_arcs.insert(flows_out_of_nodes[node][flow]);
				}
			}
		}

		std::cout << "got the used arcs:" << std::endl;
		for(std::set<uint>::iterator sol=used_arcs.begin(); sol!=used_arcs.end(); ++sol)
			std::cout << *sol << std::endl;
		std::cout << std::endl;

		// construct the directed edges out of the used arcs
		std::vector<std::vector<int> > directed_edges; // vector that stores the directed edges for each node
		for(uint start_node=0; start_node<flows_out_of_nodes.size(); ++start_node)
		{
			// check if a used arc is flowing out of the current start node
			std::vector<uint> originating_flows;
			bool originating = false;
			for(std::set<uint>::iterator arc=used_arcs.begin(); arc!=used_arcs.end(); ++arc)
			{
				if(contains(flows_out_of_nodes[start_node], *arc)==true)
				{
					originating = true;
					originating_flows.push_back(*arc);
				}
			}

			// if the arc is originating from this node, find its destination
			std::vector<int> current_directed_edges;
			if(originating==true)
			{
				for(uint end_node=0; end_node<flows_into_nodes.size(); ++end_node)
				{
					if(end_node==start_node)
						continue;

					for(std::vector<uint>::iterator arc=originating_flows.begin(); arc!=originating_flows.end(); ++arc)
						if(contains(flows_into_nodes[end_node], *arc)==true)
							current_directed_edges.push_back(end_node);
				}
			}

			// if the current node doesn't flow into another node insert a vector storing -1
			if(current_directed_edges.size()==0)
				current_directed_edges.push_back(-1);

			// save the found used directed edges
			directed_edges.push_back(current_directed_edges);
		}

		// testing
		std::cout << "used destinations: " << std::endl;
		directed_edges[1].push_back(0);
		for(size_t i=0; i<directed_edges.size(); ++i)
		{
			for(size_t j=0; j<directed_edges[i].size(); ++j)
			{
				std::cout << directed_edges[i][j] << " ";
			}
			std::cout << std::endl;
		}
		std::cout << std::endl;

		// construct the support graph out of the directed edges
		directedGraph support_graph(flows_out_of_nodes.size()); // initialize with the right number of edges

		int number_of_not_used_nodes = 0;
		for(size_t start=0; start<directed_edges.size(); ++start)
		{
			for(size_t end=0; end<directed_edges[start].size(); ++end)
			{
				// if no destination starting from this node could be found ignore this node
				if(directed_edges[start][end]==-1)
				{
					break;
					++number_of_not_used_nodes;
				}

				// add the directed edge
				boost::add_edge(start, directed_edges[start][end], support_graph);
			}
		}

		// search for the strongly connected components
		std::vector<int> c(flows_into_nodes.size());
		int number_of_strong_components = boost::strong_components(support_graph, boost::make_iterator_property_map(c.begin(), boost::get(boost::vertex_index, support_graph), c[0]));
		std::cout << "got " << number_of_strong_components << " strongly connected components" << std::endl;
		for (std::vector <int>::iterator i = c.begin(); i != c.end(); ++i)
		  std::cout << "Vertex " << i - c.begin() << " is in component " << *i << std::endl;

		// check if no cycle appears in the solution, i.e. if not each node is a component of its own or a traveling
		// salesman path has been computed (not_used_nodes+1)
		if(number_of_strong_components==flows_out_of_nodes.size() || number_of_strong_components==number_of_not_used_nodes+1)
			cycle_free = true;

		// if a cycle appears find it and add the prevention constraint to the problem and resolve it
		if(cycle_free==false)
		{
			// go trough the components and find components with more than 1 element in it
			std::vector<std::vector<uint> > cycles;
			for (int component=0; component<c.size(); ++component)
			{
				std::vector<uint> current_component;
				int elements = std::count(c.begin(), c.end(), c[component]);
				std::cout << c[component] << std::endl;
				if(elements>=2)
					for(std::vector<int>::iterator element=c.begin(); element!=c.end(); ++element)
						if(*element==c[component])
							current_component.push_back(element-c.begin());

				// save the found cycle
				cycles.push_back(current_component);
			}

			for(size_t i=0; i<cycles.size(); ++i)
			{
				for(size_t j=0; j<cycles[i].size(); ++i)
				{
					std::cout << cycles[i][j] << " ";
				}
				std::cout << std::endl;
			}
		}

		cycle_free = true;
	}while(cycle_free == false);

	for(size_t res=0; res<number_of_variables; ++res)
	{
//		std::cout << solution[res] << std::endl;
		C[res] = solution[res];
	}
}

// This Function checks if the given cv::Point is close enough to one cv::Point in the given vector. If one point gets found
// that this Point is nearer than the defined min_distance the function returns false to stop it immediately.
bool flowNetworkExplorator::pointClose(const std::vector<cv::Point>& points, const cv::Point& point, const double min_distance)
{
	double square_distance = min_distance * min_distance;
	for(std::vector<cv::Point>::const_iterator current_point = points.begin(); current_point != points.end(); ++current_point)
	{
		double dx = current_point->x - point.x;
		double dy = current_point->y - point.y;
		if( ((dx*dx + dy*dy)) <= square_distance)
			return true;
	}
	return false;
}

// Function that uses the flow network based method to determine a coverage path. To do so the following steps are done
//	I.	Discretize the free space into cells that have to be visited a least once by using the sampling distance given to
//		the function. Also create a flow network by sweeping a line along the y-/x-axis and creating an edge, whenever it
//		hits an obstacle. From this hit point go back along the sweep line until the distance is equal to the coverage
//		radius, because the free space should represent the area that should be totally covered. If in both directions
//		along the sweep line no point in the free space can be found, ignore it.
//	II.	Create the matrices and vectors for the optimization problem:
//		1. The weight vector w, storing the distances between edges.
//		2. The coverage matrix V, storing which cell can be covered when going along the arcs.
//			remark: A cell counts as covered, when its center is in the coverage radius around an arc.
//		3. The sets of arcs for each node, that store the incoming and outgoing arcs
// III.	Create a<nd solve the optimization problems in the following order:
//		1.	Find the start node that is closest to the given starting position. This start node is used as initial step
//			in the optimization problem.
//		2. 	Iteratively solve the weighted optimization problem to approximate the problem by a convex optimization. This
//			speeds up the solution and is done until the sparsity of the optimization variables doesn't change anymore,
//			i.e. converged, or a specific number of iterations is reached. To measure the sparsity a l^0_eps measure is
//			used, that checks |{i: c[i] <= eps}|. In each step the weights are adapted with respect to the previous solution.
//		3.	Solve the final optimization problem by discarding the arcs that correspond to zero elements in the previous
//			determined solution. This reduces the dimensionality of the problem and allows the algorithm to faster find
//			a solution.
void flowNetworkExplorator::getExplorationPath(const cv::Mat& room_map, std::vector<geometry_msgs::Pose2D>& path,
		const float map_resolution, const cv::Point starting_position, const cv::Point2d map_origin,
		const int cell_size, const geometry_msgs::Polygon& room_min_max_coordinates,
		const Eigen::Matrix<float, 2, 1>& robot_to_fow_middlepoint_vector, const float coverage_radius,
		const bool plan_for_footprint, const int sparsity_check_range)
{
	// *********** I. Discretize the free space and create the flow network ***********
	// find cell centers that need to be covered
	std::vector<cv::Point> cell_centers;
	for(size_t y=room_min_max_coordinates.points[0].y+0.5*cell_size; y<=room_min_max_coordinates.points[1].y; y+=cell_size)
		for(size_t x=room_min_max_coordinates.points[0].x+0.5*cell_size; x<=room_min_max_coordinates.points[1].x; x+=cell_size)
			if(room_map.at<uchar>(y,x)==255)
				cell_centers.push_back(cv::Point(x,y));

	// find edges for the flow network, sweeping along the y-axis
	std::vector<cv::Point> edges;
	int coverage_int = (int) std::floor(coverage_radius);
	std::cout << "y sweeping, radius: " << coverage_int << std::endl;
	for(size_t y=room_min_max_coordinates.points[0].y+coverage_int; y<=room_min_max_coordinates.points[1].y; ++y)
	{
//		cv::Mat test_map = room_map.clone();
		for(size_t x=0; x<room_map.cols; ++x)
		{
			// check if an obstacle has been found, only check outer parts of the occupied space
			if(room_map.at<uchar>(y,x)==0 && (room_map.at<uchar>(y-1,x)==255 || room_map.at<uchar>(y+1,x)==255))
			{
//				cv::circle(test_map, cv::Point(x,y), 2, cv::Scalar(127), CV_FILLED);
				// check on both sides along the sweep line if a free point is available, don't exceed matrix dimensions
				if(room_map.at<uchar>(y-coverage_int, x)==255 && y-coverage_int>=0)
					edges.push_back(cv::Point(x, y-coverage_int));
				else if(room_map.at<uchar>(y+coverage_int, x)==255 && y+coverage_int<room_map.rows)
					edges.push_back(cv::Point(x, y+coverage_int));

				// increase x according to the coverage radius, -1 because it gets increased after this for step
				x += 2.0*coverage_int-1;
			}
		}
//		cv::imshow("test", test_map);
//		cv::waitKey();
	}

	// sweep along x-axis
//	std::cout << "x sweeping" << std::endl;
//	for(size_t x=room_min_max_coordinates.points[0].x+coverage_int; x<=room_min_max_coordinates.points[1].x; ++x)
//	{
////		cv::Mat test_map = room_map.clone();
//		for(size_t y=0; y<room_map.rows; ++y)
//		{
//			// check if an obstacle has been found, only check outer parts of the occupied space
//			if(room_map.at<uchar>(y,x)==0 && (room_map.at<uchar>(y,x-1)==255 || room_map.at<uchar>(y,x+1)==255))
//			{
////				cv::circle(test_map, cv::Point(x,y), 2, cv::Scalar(127), CV_FILLED);
//				// check on both sides along the sweep line if a free point is available, don't exceed matrix dimensions
//				if(room_map.at<uchar>(y, x-coverage_int)==255 && x-coverage_int>=0)
//					edges.push_back(cv::Point(x-coverage_int, y));
//				else if(room_map.at<uchar>(y, x+coverage_int)==255 && x+coverage_int<room_map.cols)
//					edges.push_back(cv::Point(x+coverage_int, y));
//
//				// increase y according to the coverage radius, -1 because it gets increased after this for step
//				y += 2.0*coverage_int-1;
//			}
//		}
////		cv::imshow("test", test_map);
////		cv::waitKey();
//	}
	std::cout << "found " << edges.size() << " edges" << std::endl;

	// create the arcs for the flow network
	// TODO: reduce dimensionality, maybe only arcs that are straight (close enough to straight line)?
	std::cout << "Constructing distance matrix" << std::endl;
	cv::Mat distance_matrix; // determine weights
	DistanceMatrix::constructDistanceMatrix(distance_matrix, room_map, edges, 0.25, 0.0, map_resolution, path_planner_);
	std::cout << "Constructed distance matrix, defining arcs" << std::endl;
	std::vector<arcStruct> arcs;
	double max_distance = room_min_max_coordinates.points[1].y - room_min_max_coordinates.points[0].y; // arcs should at least go the maximal room distance to allow straight arcs
	for(size_t start=0; start<distance_matrix.rows; ++start)
	{
		for(size_t end=0; end<distance_matrix.cols; ++end)
		{
			// don't add arc from node to itself, only consider upper triangle of the distance matrix, one path from edge
			// to edge provides both arcs
			if(start!=end && end>start)
			{
				arcStruct current_forward_arc;
				current_forward_arc.start_point = edges[start];
				current_forward_arc.end_point = edges[end];
				current_forward_arc.weight = distance_matrix.at<double>(start, end);
				arcStruct current_backward_arc;
				current_backward_arc.start_point = edges[end];
				current_backward_arc.end_point = edges[start];
				current_forward_arc.weight = distance_matrix.at<double>(end, start);
				cv::Point vector = current_forward_arc.start_point - current_forward_arc.end_point;
				// don't add too long arcs to reduce dimensionality, because they certainly won't get chosen anyway
				// also don't add arcs that are too far away from the straight line (start-end) because they are likely
				// to go completely around obstacles and are not good
				if(current_forward_arc.weight <= max_distance && current_forward_arc.weight <= 1.1*cv::norm(vector)) // TODO: param
				{
					std::vector<cv::Point> astar_path;
					path_planner_.planPath(room_map, current_forward_arc.start_point, current_forward_arc.end_point, 1.0, 0.0, map_resolution, 0, &astar_path);
					current_forward_arc.edge_points = astar_path;
					// reverse path for backward arc
					std::reverse(astar_path.begin(), astar_path.end());
					current_backward_arc.edge_points = astar_path;
					arcs.push_back(current_forward_arc);
					arcs.push_back(current_backward_arc);
				}
			}
		}
	}
	std::cout << "arcs: " << arcs.size() << std::endl;

	// *********** II. Construct the matrices for the optimization problem ***********
	std::cout << "Starting to construct the matrices for the optimization problem." << std::endl;
	// 1. weight vector
	int number_of_candidates = arcs.size();
	std::vector<double> w(number_of_candidates);
	for(std::vector<arcStruct>::iterator arc=arcs.begin(); arc!=arcs.end(); ++arc)
		w[arc-arcs.begin()] = arc->weight;

	// 2. visibility matrix, storing which call can be covered when going along the arc
	//		remark: a cell counts as covered, when the center of each cell is in the coverage radius around the arc
	cv::Mat V = cv::Mat(cell_centers.size(), number_of_candidates, CV_8U); // binary variables
	for(std::vector<arcStruct>::iterator arc=arcs.begin(); arc!=arcs.end(); ++arc)
	{
		// use the pointClose function to check if a cell can be covered along the path
		for(std::vector<cv::Point>::iterator cell=cell_centers.begin(); cell!=cell_centers.end(); ++cell)
		{
			if(pointClose(arc->edge_points, *cell, 1.1*coverage_radius) == true)
				V.at<uchar>(cell-cell_centers.begin(), arc-arcs.begin()) = 1;
			else
				V.at<uchar>(cell-cell_centers.begin(), arc-arcs.begin()) = 0;
		}
	}

	// 3. set of arcs (indices) that are going into and out of one node
	std::vector<std::vector<uint> > flows_into_nodes(edges.size());
	std::vector<std::vector<uint> > flows_out_of_nodes(edges.size());
	int number_of_outflows = 0;
	for(std::vector<cv::Point>::iterator edge=edges.begin(); edge!=edges.end(); ++edge)
	{
		for(std::vector<arcStruct>::iterator arc=arcs.begin(); arc!=arcs.end(); ++arc)
		{
			// if the start point of the arc is the edge save it as incoming flow
			if(arc->start_point == *edge)
			{
				flows_into_nodes[edge-edges.begin()].push_back(arc-arcs.begin());
			}
			// if the end point of the arc is the edge save it as outgoing flow
			else if(arc->end_point == *edge)
			{
				flows_out_of_nodes[edge-edges.begin()].push_back(arc-arcs.begin());
				++number_of_outflows;
			}
		}
	}

//	testing
//	for(size_t i=0; i<flows_into_nodes.size(); ++i)
//	{
//		std::cout << "in: " << std::endl;
//		for(size_t j=0; j<flows_into_nodes[i].size(); ++j)
//			std::cout << flows_into_nodes[i][j] << std::endl;
//		std::cout << "out: " << std::endl;
//		for(size_t j=0; j<flows_out_of_nodes[i].size(); ++j)
//			std::cout << flows_out_of_nodes[i][j] << std::endl;
//		std::cout << std::endl;
//	}
//	for(size_t node=0; node<flows_out_of_nodes.size(); ++node)
//	{
//		cv::Mat paths = room_map.clone();
//		for(size_t flow=0; flow<flows_out_of_nodes[node].size(); ++flow)
//		{
//			std::vector<cv::Point> path = arcs[flows_out_of_nodes[node][flow]].edge_points;
//			for(size_t p=0; p<path.size(); ++p)
//				paths.at<uchar>(path[p]) = 127;
//		}
//		cv::imshow("paths", paths);
//		cv::waitKey();
//	}

	std::cout << "Constructed all matrices for the optimization problem. Checking if all cells can be covered." << std::endl;

	// print out warning if a defined cell is not coverable with the chosen arcs
	bool all_cells_covered = true;
	for(size_t row=0; row<V.rows; ++row)
	{
		int number_of_paths = 0;
		for(size_t col=0; col<V.cols; ++col)
			if(V.at<uchar>(row, col)==1)
				++number_of_paths;
		if(number_of_paths==0)
		{
			std::cout << "!!!!!!!! EMPTY ROW OF VISIBILITY MATRIX !!!!!!!!!!!!!" << std::endl << "cell " << row << " not coverable" << std::endl;
			all_cells_covered = false;
		}
	}
	if(all_cells_covered == false)
		std::cout << "!!!!! WARNING: Not all cells could be covered with the given parameters, try changing them or ignore it to not cover the whole free space." << std::endl;

	// *********** III. Solve the different optimization problems ***********
	// 1. Find the start node closest to the starting position.
	double min_distance = 1e5;
	uint start_index = 0;
	for(std::vector<cv::Point>::iterator edge=edges.begin(); edge!=edges.end(); ++edge)
	{
		cv::Point difference_vector = *edge - starting_position;
		double current_distance = cv::norm(difference_vector);
		if(current_distance<min_distance)
		{
			min_distance = current_distance;
			start_index = edge-edges.begin();
		}
	}

	// 2. iteratively solve the optimization problem, using convex relaxation
	std::vector<double> C(2.0*(flows_out_of_nodes[start_index].size()+number_of_candidates) + number_of_outflows + edges.size());
	std::cout << "number of outgoing arcs: " << number_of_outflows << std::endl;

//	solveThreeStageOptimizationProblem(C, V, w, flows_into_nodes, flows_out_of_nodes, flows_out_of_nodes[start_index]);
	solveLazyConstraintOptimizationProblem(C, V, w, flows_into_nodes, flows_out_of_nodes, flows_out_of_nodes[start_index]);

	// 3. discard the arcs corresponding to zero elements in the optimization vector and solve the final optimization
	//	  problem
	cv::Mat test_map = room_map.clone();
	std::set<uint> used_arcs; // set that stores the indices of the arcs corresponding to non-zero elements in the solution
	// go trough the start arcs and determine the new start arcs
	for(size_t start_arc=0; start_arc<flows_out_of_nodes[start_index].size(); ++start_arc)
	{
		if(C[start_arc]!=0)
		{
			// insert start index
			used_arcs.insert(flows_out_of_nodes[start_index][start_arc]);

			std::vector<cv::Point> path=arcs[flows_out_of_nodes[start_index][start_arc]].edge_points;
			for(size_t j=0; j<path.size(); ++j)
				test_map.at<uchar>(path[j])=50;

			cv::imshow("discretized", test_map);
			cv::waitKey();
		}
	}

	// go trough the coverage stage
	for(size_t arc=flows_out_of_nodes[start_index].size(); arc<flows_out_of_nodes[start_index].size()+arcs.size(); ++arc)
	{
		if(C[arc]!=0)
		{
			// insert index, relative to the first coverage variable
			used_arcs.insert(arc-flows_out_of_nodes[start_index].size());

			std::vector<cv::Point> path=arcs[arc-flows_out_of_nodes[start_index].size()].edge_points;
			for(size_t j=0; j<path.size(); ++j)
				test_map.at<uchar>(path[j])=100;

			cv::imshow("discretized", test_map);
			cv::waitKey();
		}
	}

	// go trough the final stage and find the remaining used arcs
	std::vector<std::vector<uint> > reduced_outflows(flows_out_of_nodes.size());
	for(size_t node=0; node<flows_out_of_nodes.size(); ++node)
	{
		for(size_t flow=0; flow<flows_out_of_nodes[node].size(); ++flow)
		{
			if(C[flows_out_of_nodes[node][flow]+flows_out_of_nodes[start_index].size()+V.cols]!=0)
			{
				// insert saved outgoing flow index
				used_arcs.insert(flows_out_of_nodes[node][flow]);
				std::vector<cv::Point> path=arcs[flows_out_of_nodes[node][flow]].edge_points;
				for(size_t j=0; j<path.size(); ++j)
					test_map.at<uchar>(path[j])=150;

				cv::imshow("discretized", test_map);
				cv::waitKey();
			}
		}
	}

	std::cout << "got " << used_arcs.size() << " used arcs" << std::endl;

	// go trough the indices of the used arcs and save the arcs as new candidates, also determine the nodes that correspond
	// to these arcs (i.e. are either start or end)
	std::vector<arcStruct> reduced_arc_candidates;
	std::vector<cv::Point> reduced_edges;
	for(std::set<uint>::iterator candidate=used_arcs.begin(); candidate!=used_arcs.end(); ++candidate)
	{
		arcStruct current_arc = arcs[*candidate];
		cv::Point start = current_arc.start_point;
		cv::Point end = current_arc.end_point;
		reduced_arc_candidates.push_back(current_arc);

		// if the start/end hasn't been already saved, save it
		if(contains(reduced_edges, start)==false)
			reduced_edges.push_back(start);
		if(contains(reduced_edges, end)==false)
			reduced_edges.push_back(end);
	}

	// determine the reduced outgoing and incoming flows and find new start index
	std::vector<std::vector<uint> > reduced_flows_into_nodes(reduced_edges.size());
	std::vector<std::vector<uint> > reduced_flows_out_of_nodes(reduced_edges.size());
	uint reduced_start_index = 0;
	for(std::vector<cv::Point>::iterator edge=reduced_edges.begin(); edge!=reduced_edges.end(); ++edge)
	{
		for(std::vector<arcStruct>::iterator arc=reduced_arc_candidates.begin(); arc!=reduced_arc_candidates.end(); ++arc)
		{
			// if the start point of the arc is the edge save it as incoming flow
			if(arc->start_point == *edge)
			{
				reduced_flows_into_nodes[edge-reduced_edges.begin()].push_back(arc-reduced_arc_candidates.begin());

				// check if current origin of the arc is determined start edge
				if(*edge == edges[start_index])
				{
					reduced_start_index = edge-reduced_edges.begin();
					std::cout << "found new start index" << std::endl;
				}
			}
			// if the end point of the arc is the edge save it as outgoing flow
			else if(arc->end_point == *edge)
				reduced_flows_out_of_nodes[edge-reduced_edges.begin()].push_back(arc-reduced_arc_candidates.begin());
		}
	}

	std::cout << "number of arcs (" << reduced_flows_out_of_nodes.size() << ") for the reduced edges:" << std::endl;
	for(size_t i=0; i<reduced_flows_out_of_nodes.size(); ++i)
		std::cout << "n" << (int) i << ": " << reduced_flows_out_of_nodes[i].size() << std::endl;

	// remove the first initial column
	uint new_number_of_variables = 0;
	cv::Mat V_reduced = cv::Mat(cell_centers.size(), 1, CV_8U); // initialize one column because opencv wants it this way, add other columns later
	for(std::set<uint>::iterator var=used_arcs.begin(); var!=used_arcs.end(); ++var)
	{
		// gather column corresponding to this candidate pose and add it to the new observability matrix
		cv::Mat column = V.col(*var);
		cv::hconcat(V_reduced, column, V_reduced);
	}
	V_reduced = V_reduced.colRange(1, V_reduced.cols);

	for(size_t sol=0; sol<flows_out_of_nodes[start_index].size()+2.0*number_of_candidates; ++sol)
	{
		if(C[sol] != 0)
		{
			std::cout << "var" << sol << ": " << C[sol] << std::endl;
		}
	}

	for(size_t row=0; row<V_reduced.rows; ++row)
	{
		int one_count = 0;
		for(size_t col=0; col<V_reduced.cols; ++col)
		{
//			std::cout << (int) V_reduced.at<uchar>(row, col) << " ";
			if(V_reduced.at<uchar>(row, col)!=0)
				++one_count;
		}
//		std::cout << std::endl;
		if(one_count == 0)
			std::cout << "!!!!!!!!!!!!! empty row !!!!!!!!!!!!!!!!!!" << std::endl;
	}

//	testing
//	cv::Mat test_map = room_map.clone();
//	for(size_t i=0; i<cell_centers.size(); ++i)
//		cv::circle(test_map, cell_centers[i], 2, cv::Scalar(75), CV_FILLED);
	for(size_t i=0; i<reduced_edges.size(); ++i)
		cv::circle(test_map, reduced_edges[i], 2, cv::Scalar(150), CV_FILLED);
//	for(size_t i=0; i<reduced_arc_candidates.size(); ++i)
//	{
//		std::vector<cv::Point> path=reduced_arc_candidates[i].edge_points;
//		for(size_t j=0; j<path.size(); ++j)
//			test_map.at<uchar>(path[j])=100;
//	}
	cv::imshow("discretized", test_map);
	cv::waitKey();
}

// test function for an easy case to check correctness
void flowNetworkExplorator::testFunc()
{
//	std::vector<double> w(6, 1.0);
//	std::vector<int> C(2+6+6);
//	cv::Mat V = cv::Mat(8, 6, CV_8U, cv::Scalar(0));
//	std::vector<std::vector<uint> > flows_out_of_nodes(3);
//	std::vector<std::vector<uint> > flows_in_nodes(3);
//
//	// cell 1
//	V.at<uchar>(0,0) = 1;
//	V.at<uchar>(0,1) = 1;
//	//cell 2
//	V.at<uchar>(1,0) = 1;
//	V.at<uchar>(1,1) = 1;
//	// cell 3
//	V.at<uchar>(2,4) = 1;
//	V.at<uchar>(2,5) = 1;
//	// cell 4
//	V.at<uchar>(3,0) = 1;
//	V.at<uchar>(3,1) = 1;
//	V.at<uchar>(3,4) = 1;
//	V.at<uchar>(3,5) = 1;
//	// cell 5
//	V.at<uchar>(4,0) = 1;
//	V.at<uchar>(4,1) = 1;
//	V.at<uchar>(4,2) = 1;
//	V.at<uchar>(4,3) = 1;
//	// cell 6
//	V.at<uchar>(5,2) = 1;
//	V.at<uchar>(5,3) = 1;
//	// cell 7
//	V.at<uchar>(6,4) = 1;
//	V.at<uchar>(6,5) = 1;
//	// cell 8
//	V.at<uchar>(7,2) = 1;
//	V.at<uchar>(7,3) = 1;
//
//	flows_out_of_nodes[0].push_back(0);
//	flows_out_of_nodes[0].push_back(4);
//	flows_out_of_nodes[1].push_back(1);
//	flows_out_of_nodes[1].push_back(2);
//	flows_out_of_nodes[2].push_back(3);
//	flows_out_of_nodes[2].push_back(5);
//
//	flows_in_nodes[0].push_back(1);
//	flows_in_nodes[0].push_back(5);
//	flows_in_nodes[1].push_back(0);
//	flows_in_nodes[1].push_back(3);
//	flows_in_nodes[2].push_back(2);
//	flows_in_nodes[2].push_back(4);
//
//	for(size_t row=0; row<V.rows; ++row)
//	{
//		for(size_t col=0; col<V.cols; ++col)
//		{
//			std::cout << (int) V.at<uchar>(row, col) << " ";
//		}
//		std::cout << std::endl;
//	}
	std::vector<double> w(14, 1.0);
	std::vector<double> C(2+14+14+2+14+6);
	cv::Mat V = cv::Mat(12, 14, CV_8U, cv::Scalar(0));
	std::vector<std::vector<uint> > flows_out_of_nodes(6);
	std::vector<std::vector<uint> > flows_in_nodes(6);

	// cell 1
	V.at<uchar>(0,0) = 1;
	V.at<uchar>(0,1) = 1;
	//cell 2
	V.at<uchar>(1,0) = 1;
	V.at<uchar>(1,1) = 1;
	V.at<uchar>(1,4) = 1;
	V.at<uchar>(1,5) = 1;
	// cell 3
	V.at<uchar>(2,4) = 1;
	V.at<uchar>(2,5) = 1;
	V.at<uchar>(2,10) = 1;
	V.at<uchar>(2,11) = 1;
	// cell 4
	V.at<uchar>(3,0) = 1;
	V.at<uchar>(3,1) = 1;
	// cell 5
	V.at<uchar>(4,0) = 1;
	V.at<uchar>(4,1) = 1;
	V.at<uchar>(4,6) = 1;
	V.at<uchar>(4,7) = 1;
	// cell 6
	V.at<uchar>(5,6) = 1;
	V.at<uchar>(5,7) = 1;
	V.at<uchar>(5,10) = 1;
	V.at<uchar>(5,11) = 1;
	// cell 7
	V.at<uchar>(6,2) = 1;
	V.at<uchar>(6,3) = 1;
	// cell 8
	V.at<uchar>(7,2) = 1;
	V.at<uchar>(7,3) = 1;
	V.at<uchar>(7,6) = 1;
	V.at<uchar>(7,7) = 1;
	// cell 9
	V.at<uchar>(8,6) = 1;
	V.at<uchar>(8,7) = 1;
	V.at<uchar>(8,12) = 1;
	V.at<uchar>(8,13) = 1;
	// cell 10
	V.at<uchar>(9,2) = 1;
	V.at<uchar>(9,3) = 1;
	// cell 11
	V.at<uchar>(10,2) = 1;
	V.at<uchar>(10,3) = 1;
	V.at<uchar>(10,8) = 1;
	V.at<uchar>(10,9) = 1;
	// cell 12
	V.at<uchar>(11,8) = 1;
	V.at<uchar>(11,9) = 1;
	V.at<uchar>(11,12) = 1;
	V.at<uchar>(11,13) = 1;

	flows_out_of_nodes[0].push_back(0);
	flows_out_of_nodes[0].push_back(4);

	flows_out_of_nodes[1].push_back(1);
	flows_out_of_nodes[1].push_back(2);
	flows_out_of_nodes[1].push_back(6);

	flows_out_of_nodes[2].push_back(3);
	flows_out_of_nodes[2].push_back(8);

	flows_out_of_nodes[3].push_back(5);
	flows_out_of_nodes[3].push_back(10);

	flows_out_of_nodes[4].push_back(7);
	flows_out_of_nodes[4].push_back(11);
	flows_out_of_nodes[4].push_back(12);

	flows_out_of_nodes[5].push_back(9);
	flows_out_of_nodes[5].push_back(13);


	flows_in_nodes[0].push_back(1);
	flows_in_nodes[0].push_back(5);

	flows_in_nodes[1].push_back(0);
	flows_in_nodes[1].push_back(3);
	flows_in_nodes[1].push_back(7);

	flows_in_nodes[2].push_back(2);
	flows_in_nodes[2].push_back(9);

	flows_in_nodes[3].push_back(4);
	flows_in_nodes[3].push_back(11);

	flows_in_nodes[4].push_back(6);
	flows_in_nodes[4].push_back(10);
	flows_in_nodes[4].push_back(13);

	flows_in_nodes[5].push_back(8);
	flows_in_nodes[5].push_back(12);

	for(size_t row=0; row<V.rows; ++row)
	{
		for(size_t col=0; col<V.cols; ++col)
		{
			std::cout << (int) V.at<uchar>(row, col) << " ";
		}
		std::cout << std::endl;
	}

	std::vector<double> W(C.size(), 1.0);
	double weight_epsilon = 0.0;
	double euler_constant = std::exp(1.0);
	for(size_t i=1; i<=1; ++i)
	{

//		solveThreeStageOptimizationProblem(C, V, w, flows_in_nodes, flows_out_of_nodes, flows_out_of_nodes[0]);//, &W);
		solveLazyConstraintOptimizationProblem(C, V, w, flows_in_nodes, flows_out_of_nodes, flows_out_of_nodes[0]);
		for(size_t c=0; c<C.size(); ++c)
			std::cout << C[c] << std::endl;
		std::cout << std::endl;

		int exponent = 1 + (i - 1)*0.1;
		weight_epsilon = std::pow(1/(euler_constant-1), exponent);
		for(size_t weight=0; weight<W.size(); ++weight)
		{
			W[weight] = weight_epsilon/(weight_epsilon + C[weight]);
			std::cout << W[weight] << std::endl;
		}
		std::cout << std::endl;

		cv::imshow("V", V);
		cv::waitKey();
	}

	std::set<uint> used_arcs; // set that stores the indices of the arcs corresponding to non-zero elements in the solution
	// go trough the start arcs and determine the new start arcs
	std::cout << "initial: " << std::endl;
	for(size_t start_arc=0; start_arc<flows_out_of_nodes[0].size(); ++start_arc)
	{
		if(C[start_arc]!=0)
		{
			// insert start index
			used_arcs.insert(flows_out_of_nodes[0][start_arc]);
			std::cout << flows_out_of_nodes[0][start_arc] << std::endl;
		}
	}

	// go trough the coverage stage
	std::cout << "coverage: " << std::endl;
	for(size_t arc=flows_out_of_nodes[0].size(); arc<flows_out_of_nodes[0].size()+V.cols; ++arc)
	{
		if(C[arc]!=0)
		{
			// insert index, relative to the first coverage variable
			used_arcs.insert(arc-flows_out_of_nodes[0].size());

			std::cout << arc-flows_out_of_nodes[0].size() << std::endl;
		}
	}

	// go trough the final stage and find the remaining used arcs
	std::cout << "final: " << std::endl;
	for(size_t node=0; node<flows_out_of_nodes.size(); ++node)
	{
		for(size_t flow=0; flow<flows_out_of_nodes[node].size(); ++flow)
		{
			if(C[flows_out_of_nodes[node][flow]+flows_out_of_nodes[0].size()+V.cols]!=0)
			{
				// insert saved outgoing flow index
				used_arcs.insert(flows_out_of_nodes[node][flow]);

				std::cout << flows_out_of_nodes[node][flow] << std::endl;
			}
		}
	}

	std::cout << "got " << used_arcs.size() << " used arcs" << std::endl;

	// remove the first initial column
	uint new_number_of_variables = 0;
	cv::Mat V_reduced = cv::Mat(V.rows, 1, CV_8U); // initialize one column because opencv wants it this way, add other columns later
	for(std::set<uint>::iterator var=used_arcs.begin(); var!=used_arcs.end(); ++var)
	{
		// gather column corresponding to this candidate pose and add it to the new observability matrix
		cv::Mat column = V.col(*var);
		cv::hconcat(V_reduced, column, V_reduced);
	}
	V_reduced = V_reduced.colRange(1, V_reduced.cols);

	for(size_t row=0; row<V_reduced.rows; ++row)
	{
		int one_count = 0;
		for(size_t col=0; col<V_reduced.cols; ++col)
		{
			std::cout << (int) V_reduced.at<uchar>(row, col) << " ";
			if(V_reduced.at<uchar>(row, col)!=0)
				++one_count;
		}
		std::cout << std::endl;
		if(one_count == 0)
			std::cout << "!!!!!!!!!!!!! empty row !!!!!!!!!!!!!!!!!!" << std::endl;
	}

	V_reduced = V_reduced.colRange(1, V_reduced.cols);

	std::cout << "read out: " << std::endl;
	for(size_t c=0; c<C.size(); ++c)
		std::cout << C[c] << std::endl;

//	QSprob problem;
//	problem = QSread_prob("int_lin_flow_prog.lp", "LP");
//	int status=0;
//	QSget_intcount(problem, &status);
//	std::cout << "number of integer variables in the problem: " << status << std::endl;
//	int* intflags = (int *) malloc (14 * sizeof (int));
//	QSget_intflags (problem, intflags);
//    printf ("Integer Variables\n");
//    for (int j = 0; j < 14; j++)
//    {
//        if (intflags[j] == 1)
//        {
//            printf ("%d ", j);
//        }
//    }
//    printf ("\n");
//	QSopt_dual(problem, NULL);
//	double* result;
//	result  = (double *) malloc(14 * sizeof (double));
//	QSget_solution(problem, NULL, result, NULL, NULL, NULL);
//	for(size_t variable=0; variable<14; ++variable)
//	{
//		std::cout << result[variable] << std::endl;
//	}
//	QSwrite_prob(problem, "lin_flow_prog.lp", "LP");

	OsiClpSolverInterface LP_solver;
	OsiClpSolverInterface* solver_pointer = &LP_solver;

	double obj[] = {1, 1, 1, 1, 1, 1, 1, 1};
	double lower[] = {0, 0, 0, 0, 0, 0, 0, 0};
	double upper[] = {1, 1, 1, 1, 1, 1, 1, 1};
	int which_int[] = {0, 1, 2, 3, 4, 5, 6, 7};
	int initial_constr[] = {0, 1};
	int cover_constr1[] = {0, 1, 2, 3, 5, 6};
	int cover_constr2[] = {4, 7};
	int cover_constr3[] = {0, 2, 4, 5, 7};
	int cover_constr4[] = {1, 3, 4, 6, 7};
	int con_constr[] = {1, 3, 4, 7};
	int final_constr[] = {5, 6, 7};
	double init_constr_obj[] = {1, 1};
	double cover_obj1[] = {1, 1, 1, 1, 1, 1};
	double cover_obj2[] = {1, 1};
	double cover_obj3[] = {1, 1, 1, 1, 1};
	double cover_obj4[] = {1, 1, 1, 1, 1};
	double con_obj[] = {1, 1, -1, -1};
	double final_constr_obj[] = {1, 1, 1};
	int numberColumns=(int) (sizeof(lower)/sizeof(double));

	CoinModel problem_builder;

	for(size_t i=0; i<numberColumns; ++i)
	{
		problem_builder.setColBounds(i, lower[i], upper[i]);
		problem_builder.setObjective(i, obj[i]);

		problem_builder.setInteger(i);
	}

	problem_builder.addRow(2, initial_constr, init_constr_obj, 1, 1);
	problem_builder.addRow(6, cover_constr1, cover_obj1, 1);
	problem_builder.addRow(2, cover_constr2, cover_obj2, 1);
	problem_builder.addRow(5, cover_constr3, cover_obj3, 1);
	problem_builder.addRow(5, cover_constr4, cover_obj4, 1);
	problem_builder.addRow(4, con_constr, con_obj, 0, 0);
	problem_builder.addRow(3, final_constr, final_constr_obj, 1, 1);

	solver_pointer->loadFromCoinModel(problem_builder);

	solver_pointer->writeLp("test_lp", "lp");

	CbcModel model(*solver_pointer);
	model.solver()->setHintParam(OsiDoReducePrint, true, OsiHintTry);

//	CbcHeuristicLocal heuristic2(model);
//	model.addHeuristic(&heuristic2);

	model.initialSolve();
	model.branchAndBound();

	const double * solution = model.solver()->getColSolution();

	for(size_t i=0; i<numberColumns; ++i)
		std::cout << solution[i] << std::endl;
	std::cout << std::endl;

//  CglProbing generator1;
//  generator1.setUsingObjective(true);
//  generator1.setMaxPass(3);
//  generator1.setMaxProbe(100);
//  generator1.setMaxLook(50);
//  generator1.setRowCuts(3);
}
