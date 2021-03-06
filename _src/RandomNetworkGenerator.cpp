//
// Created by Ciaran on 09/09/2020.
//

#include "evo/logger.h"
#include <chrono>
#include <regex>
#include <evo/RandomNetworkGenerator.h>
#include <evo/logger.h>
#include "mpi.h"


namespace evo {

/************************************************************************
 * Constructors
 */

    RandomNetworkGenerator::RandomNetworkGenerator(const RandomNetworkGeneratorOptions &options)
            : options_(std::make_unique<RandomNetworkGeneratorOptions>(options)) {
        nc::random::seed(options.getSeed());
    }

    const std::unique_ptr<RandomNetworkGeneratorOptions> &RandomNetworkGenerator::getOptions() const {
        return options_;
    }

/************************************************************************
 * Getters and setters
 */

    void RandomNetworkGenerator::setOptions(std::unique_ptr<RandomNetworkGeneratorOptions> &options) {
        options_ = std::move(options);
    }

    void RandomNetworkGenerator::setOptions(const RandomNetworkGeneratorOptions &options) {
        options_ = std::make_unique<RandomNetworkGeneratorOptions>(options);
    }

/************************************************************************
 * Private member functions
 */

    IndividualPtr RandomNetworkGenerator::generate() {
        // fist create a roadrunner model to work with
        std::unique_ptr<RoadRunner> rr_ptr = createRRModel();
        Compartments compartments = createCompartments();
        for (int i = 0; i < compartments.ids.size(); i++) {
            rr_ptr->addCompartment(
                    compartments.ids[i],
                    compartments.values[i],
                    false);
        }

        // create boundary species
        BoundarySpecies boundarySpecies = createBoundarySpecies();
        for (int i = 0; i < boundarySpecies.ids.size(); i++) {
            rr_ptr->addSpecies(
                    boundarySpecies.ids[i],
                    compartments.ids[boundarySpecies.compartment_index[i]],
                    boundarySpecies.values[i],
                    false,
                    true,
                    "",
                    false
            );
        }

        // create floating species
        FloatingSpecies floatingSpecies = createFloatingSpecies();
        for (int i = 0; i < floatingSpecies.ids.size(); i++) {
            rr_ptr->addSpecies(
                    floatingSpecies.ids[i],
                    compartments.ids[floatingSpecies.compartment_index[i]],
                    floatingSpecies.values[i],
                    false,
                    false,
                    "",
                    false
            );
        }

        // create reactions
        Reactions reactions = createReactions();
        for (int reaction_number = 0; reaction_number < options_->getNReactions(); reaction_number++) {
            const EvoRateLaw &rateLaw = reactions.rate_laws[reaction_number];
            std::string rate_law_string = rateLaw.getRateLawString(); // need copy not reference as we modify this string
            const IntVector &substrate_idx_vector = reactions.substrates[reaction_number];
            const IntVector &product_idx_vector = reactions.products[reaction_number];
            const IntVector &modifier_idx_vector = reactions.modifiers[reaction_number];

            // setup some integers to count the current index of substrate, product etc
            int substrate_count = 0;
            int product_count = 0;
            int modifier_count = 0;

            // and setup a place to store the string representation of the various components
            StringVector substrate_string_vector;
            StringVector product_string_vector;
            // remember that modifers are infered from rate law internally inside roadrunner

            const RoleMap &roles = rateLaw.getRoles();

            for (const auto &role_pair: roles) {
                const std::string &rate_law_component = role_pair.first;
                const RoleType &role = role_pair.second;
                // Regex defined here as its the same expression for all cases.
                // \b matches a word boundary (\\b escapes the first \)
                std::regex reg(rate_law_component + "\\b");//
                switch (role) {
                    case (EVO_PARAMETER) : {
                        // sample between specified boundaries and add to the model
                        auto val = nc::random::uniform<double>(options_->getParameterLowerBound(),
                                                               options_->getParameterUpperBound());
                        // ensure unique parameter name. We pass in reference to used parameter names
                        // so we do not have to reload the model each time we add a parameter (performance reasons).
                        std::string parameter_name = generateUniqueParameterID(current_parameter_id_number_,
                                                                               rate_law_component);
                        // remember to keep track of last used id.
                        current_parameter_id_number_++;
                        existing_model_parameters_[parameter_name] = val;
                        // now we add the parameter to the model
                        rr_ptr->addParameter(parameter_name, val, false);
                        // now ensure the placeholder (it.first) contains the newly generated parameter name
                        rate_law_string = regex_replace(rate_law_string, reg, parameter_name);
                        break;
                    }
                    case (EVO_SUBSTRATE) : {
                        const int &substrate_idx = substrate_idx_vector[substrate_count];
                        substrate_count++;
                        // convert the index to string
                        const std::string &substrate = convertSpeciesIndexToString(boundarySpecies, floatingSpecies,
                                                                                   substrate_idx);
                        substrate_string_vector.push_back(substrate);
                        rate_law_string = regex_replace(rate_law_string, reg, substrate);
                        break;
                    }
                    case (EVO_MODIFIER) : {
                        const int &modifier_idx = modifier_idx_vector[modifier_count];
                        modifier_count++;
                        // convert the index to string
                        const std::string &modifier = convertSpeciesIndexToString(boundarySpecies, floatingSpecies,
                                                                                  modifier_idx);
                        rate_law_string = regex_replace(rate_law_string, reg, modifier);
                        break;
                    }
                    case (EVO_PRODUCT) : {
                        const int &product_idx = product_idx_vector[product_count];
                        product_count++;
                        // convert the index to string
                        const std::string &product = convertSpeciesIndexToString(boundarySpecies, floatingSpecies,
                                                                                 product_idx);
                        product_string_vector.push_back(product);
                        rate_law_string = regex_replace(rate_law_string, reg, product);
                        break;
                    }
                    default : {
                        INVALID_ARGUMENT_ERROR << "Unknown role given";
                    }
                }
            }
            rr_ptr->addReaction(
                    reactions.ids[reaction_number], substrate_string_vector, product_string_vector, rate_law_string,
                    false
            );
        }
        rr_ptr->regenerate();
        Individual individual(std::move(rr_ptr));

        return std::make_unique<Individual>(std::move(individual));
    }

    NestedIndividualPtrVector RandomNetworkGenerator::generate(int N) {

        // initialize MPI
        MPI_Init(nullptr, nullptr);

        // Find out rank and world size
        int world_rank;
        int world_size;// injected into program with mpirun command. Default is 1.

        MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
        MPI_Comm_size(MPI_COMM_WORLD, &world_size);

        // create our storage structure to have the world_size elements
        NestedIndividualPtrVector rr_vec(world_size);

        int num_per_proc = N / world_size;
        int r = N % world_size;

        // todo think about how we can generalize this logic so that we do
        // not need to rewrite it for everything that we do with mpi.
        // note it might be possible to follow the decorator pattern.
        for (int rank = 0; rank < world_size; rank++) {
            // only when this nodes rank comes up do we do anything
            if (rank == world_rank) {
                int nstart;
                int nend;
                if (rank < r) {// handle remainder
                    nstart = rank * (num_per_proc + 1);
                    nend = nstart + num_per_proc;
                } else {
                    nstart = rank * num_per_proc + r;
                    nend = nstart + (num_per_proc - 1);
                }
                for (int i = nstart; i < nend + 1; i++) { // nend + 1 so that we have open intervals (0, 3) for instance is 4 items
                    std::unique_ptr<Individual> individual = generate();
                    // we set the rank of the individual and move it back
                    individual->setRank(rank);
                    rr_vec[rank].push_back(std::move(individual));
                }
            }
        }
        MPI_Finalize();
        return rr_vec;
    }

    std::unique_ptr<RoadRunner> RandomNetworkGenerator::createRRModel() const {
        if (options_->getCoreSBML().empty()) {
            auto *rr = new RoadRunner();
            return std::move(std::unique_ptr<RoadRunner>(rr));
        } else {
            NOT_IMPLEMENTED_ERROR << "Generating a random network around a core "
                                     "sbml model is not yet implemented";
//            auto *rr = new RoadRunner(options_->getCoreSBML());
//            existing_model_parameters_ = getExistingModelParameters(*rr);
//            return std::move(std::unique_ptr<RoadRunner>(rr));
        }
    }

/************************************************************************
 * Protected member functions
 */
    EvoRateLaw RandomNetworkGenerator::getRandomRateLaw() const {
        std::vector<EvoRateLaw> keys;
        for (auto &it : options_->getRateLaws()) {
            keys.push_back(it.second);
        }

        if (options_->getRateLaws().empty()) {
            INVALID_ARGUMENT_ERROR << "The RateLaw field of RandomNetworkGeneratorOptions is "
                                      "empty. Please prodive some rate laws to continue";
        }
        int random_rate_law_index = nc::random::randInt<int>(0, (int) options_->getRateLaws().size());
        return keys[random_rate_law_index];
    }

    std::vector<int> RandomNetworkGenerator::selectRandomSpeciesIndex(int n) const {
        int nspecies = options_->getNBoundarySpecies() + options_->getNFloatingSpecies();

        // check for the impossible
        if (n > nspecies) {
            std::ostringstream err;
            err << "Requested selecting "
                << n
                << " random boundary species without replacement but only "
                << nspecies << " species exist.";
            LOGIC_ERROR << err.str();
        }
        // do the sampling
        std::vector<int> species_indices = sample_with_replacement(n, nspecies);
        return species_indices;
    }

    std::vector<int> RandomNetworkGenerator::sample_with_replacement(int nsamples, int npop) {
        if (nsamples > npop) {
            LOGIC_ERROR << "The number of samples cannot be more than the size of the population when sampling"
                           "with replacement";
        }

        std::vector<int> out;
        for (int j = npop - nsamples + 1; j < npop + 1; j++) {
            auto random_number = nc::random::uniform<double>(0.0, 1.0);
            unsigned int t = 1 + (int) j * random_number;
            if (std::find(out.begin(), out.end(), t) != out.end()) {
                out.push_back(j);
            } else {
                out.push_back(t);
            }
        }

        // we subtract 1 from the output so that we have index friendly numbers
        // i.e. algorithm is 1 indexed and C++ is 0 indexed.
        for (int &i : out) {
            i -= 1;
        }
        return out;
    }

    std::string
    RandomNetworkGenerator::generateUniqueParameterID(unsigned long long number, const std::string &base_name) const {
        StringVector existing_parameter_ids;
        for (auto &i: existing_model_parameters_)
            existing_parameter_ids.push_back(i.first);

        std::ostringstream proposal;
        proposal << base_name << number;
        if (std::find(existing_parameter_ids.begin(), existing_parameter_ids.end(), proposal.str()) !=
            existing_parameter_ids.end()) {
            // id already used
            number += 1;
            proposal.str("");// clear the proposal

            return generateUniqueParameterID(number, base_name);
        } else {
            return proposal.str();
        }
    }

    std::string RandomNetworkGenerator::convertSpeciesIndexToString(const BoundarySpecies &boundarySpecies,
                                                                    const FloatingSpecies &floatingSpecies,
                                                                    int idx) {
        StringVector species_ids = boundarySpecies.ids;
        species_ids.insert(species_ids.begin(), floatingSpecies.ids.begin(), floatingSpecies.ids.end());
        if (idx < boundarySpecies.ids.size()) {
            return boundarySpecies.ids[idx];
        } else {
            return floatingSpecies.ids[idx - boundarySpecies.ids.size()]; // offset by num boundary species
        }
    }


    /************************************************************************
     * NaiveRandomNetworkGenerator2
     */

    Compartments NaiveRandomNetworkGenerator::createCompartments() {
        Compartments compartments;
        std::ostringstream id;
        for (int i = 0; i < options_->getNCompartments(); i++) {
            double val = options_->getCompartmentUpperBound();
            if (options_->getCompartmentLowerBound() != options_->getCompartmentUpperBound()) {
                val = nc::random::uniform<double>(options_->getCompartmentLowerBound(),
                                                  options_->getCompartmentUpperBound());
            }
            id << "C" << i;
            compartments.ids.push_back(id.str());
            compartments.values.push_back(val);
            id.str(std::string());// clear the stream
        }
        return compartments;
    }

    FloatingSpecies NaiveRandomNetworkGenerator::createFloatingSpecies() {
        FloatingSpecies floatingSpecies;
        std::ostringstream id;
        for (int i = 0; i < options_->getNFloatingSpecies(); i++) {
            id << "S" << i;
            double val = options_->getSpeciesUpperBound();
            if (options_->getSpeciesLowerBound() != options_->getSpeciesUpperBound()) {
                val = nc::random::uniform<double>(options_->getSpeciesLowerBound(),
                                                  options_->getSpeciesUpperBound());
            }
            // With single compartment models, go and get the compartment name
            // with multiple compartment models, random selection
            int comp_index;
            if (options_->getNCompartments() == 1) {
                comp_index = 0;
            } else {
                // pick a number between 0 and num compartments
                comp_index = nc::random::randInt(0, options_->getNCompartments());
            }
            floatingSpecies.ids.push_back(id.str());
            floatingSpecies.values.push_back(val);
            floatingSpecies.compartment_index.push_back(comp_index);
            id.str(std::string());
        }

        return floatingSpecies;
    }

    BoundarySpecies NaiveRandomNetworkGenerator::createBoundarySpecies() {
        BoundarySpecies boundarySpecies;
        std::ostringstream id;
        for (int i = 0; i < options_->getNBoundarySpecies(); i++) {
            id << "I" << i;
            double val = options_->getBoundarySpeciesUpperBound();
            if (options_->getBoundarySpeciesLowerBound() != options_->getBoundarySpeciesUpperBound()) {
                val = nc::random::uniform<double>(options_->getBoundarySpeciesLowerBound(),
                                                  options_->getBoundarySpeciesUpperBound());
            }
            // With single compartment models, go and get the compartment name
            // with multiple compartment models, random selection
            int comp_index;
            if (options_->getNCompartments() == 1) {
                comp_index = 0;
            } else {
                // pick a number between 0 and num compartments
                comp_index = nc::random::randInt(0, options_->getNCompartments());
            }
            boundarySpecies.ids.push_back(id.str());
            boundarySpecies.values.push_back(val);
            boundarySpecies.compartment_index.push_back(comp_index);
            id.str(std::string());
        }
        return boundarySpecies;
    }

    Reactions NaiveRandomNetworkGenerator::createReactions() {
        Reactions reactions(options_->getNReactions());

        std::ostringstream reaction_name;
        for (int reaction_number = 0; reaction_number < options_->getNReactions(); reaction_number++) {
            // generate reaction name;
            reaction_name << "R" << reaction_number;
            reactions.ids[reaction_number] = reaction_name.str();
            reaction_name.str("");// clear the stream

            // select a random rate law
            EvoRateLaw rateLaw = getRandomRateLaw();
            reactions.rate_laws[reaction_number] = rateLaw;

            const RoleMap &roles = rateLaw.getRoles();// from user input

            // work out how many randomly selected species we need
            int num_random_species =
                    rateLaw.numSubstrates() + rateLaw.numProducts() + rateLaw.numModifiers();
            int total_num_species_possible = options_->getNBoundarySpecies() + options_->getNFloatingSpecies();

            // check that it makes sense to randomly generate num_random_species species
            if (num_random_species > total_num_species_possible) {
                const std::string &name = rateLaw.getName();
                LOGIC_ERROR << "Rate law \"" << name << "\" requires " << num_random_species
                            << " species "
                            << "but your configurations only allow for " << total_num_species_possible
                            << ". Please change your configuration options either by allowing more Floating or Boundary species or "
                               "using different rate laws.";
            }

            // randomly sample without replacement
            std::vector<int> species_indices = selectRandomSpeciesIndex(num_random_species);
            assert(species_indices.size() == num_random_species); // this will always be true

            // dish out the species indices to reaction substrates, products or modifiers.
            for (int s = 0; s < rateLaw.numSubstrates(); s++) {
                const int &species_idx = species_indices[species_indices.size() - 1];
                reactions.substrates[reaction_number].push_back(species_idx);
                species_indices.resize(species_indices.size() - 1);
            }
            for (int s = 0; s < rateLaw.numProducts(); s++) {
                const int &species_idx = species_indices[species_indices.size() - 1];
                reactions.products[reaction_number].push_back(species_idx);
                species_indices.resize(species_indices.size() - 1);
            }
            for (int s = 0; s < rateLaw.numModifiers(); s++) {
                const int &species_idx = species_indices[species_indices.size() - 1];
                reactions.modifiers[reaction_number].push_back(species_idx);
                species_indices.resize(species_indices.size() - 1);
            }
            assert(species_indices.empty());
        }
        return reactions;
    }

/*****************************************************************************
 * UniqueReactionsRandomNetworkGenerator
 */

    UniqueReactionsRandomNetworkGenerator::UniqueReactionsRandomNetworkGenerator(
            const RandomNetworkGeneratorOptions &options, int max_recursion)
            : NaiveRandomNetworkGenerator(options), max_recursion_(max_recursion) {}

    Reactions UniqueReactionsRandomNetworkGenerator::createReactions() {
        NOT_IMPLEMENTED_ERROR << "This class does not yet work";
        Reactions reactions(options_->getNReactions());

        std::ostringstream reaction_name;
        int reaction_number = 0;
        int recursion_count = 0;

        while (reactions.size() != options_->getNReactions()) {
            // generate reaction name;
            reaction_name << "R" << reaction_number;

            // select a random rate law
            EvoRateLaw rateLaw = getRandomRateLaw();

            const RoleMap &roles = rateLaw.getRoles();// from user input

            // work out how many randomly selected species we need
            int num_random_species =
                    rateLaw.numSubstrates() + rateLaw.numProducts() + rateLaw.numModifiers();
            int total_num_species_possible = options_->getNBoundarySpecies() + options_->getNFloatingSpecies();

            // check that it makes sense to randomly generate num_random_species species
            if (num_random_species > total_num_species_possible) {
                const std::string &name = rateLaw.getName();
                LOGIC_ERROR << "Rate law \"" << name << "\" requires " << num_random_species
                            << " species "
                            << "but your configurations only allow for " << total_num_species_possible
                            << ". Please change your configuration options either by allowing more Floating or Boundary species or "
                               "using different rate laws.";
            }

            // randomly sample without replacement
            std::vector<int> species_indices = selectRandomSpeciesIndex(num_random_species);
            assert(species_indices.size() == num_random_species); // this will always be true

            std::vector<int> substrates;
            std::vector<int> products;
            std::vector<int> modifiers;

            // dish out the species indices to reaction substrates, products or modifiers.
            for (int s = 0; s < rateLaw.numSubstrates(); s++) {
                const int &species_idx = species_indices[species_indices.size() - 1];
                substrates.push_back(species_idx);
                species_indices.resize(species_indices.size() - 1);
            }
            for (int s = 0; s < rateLaw.numProducts(); s++) {
                const int &species_idx = species_indices[species_indices.size() - 1];
                products.push_back(species_idx);
                species_indices.resize(species_indices.size() - 1);
            }
            for (int s = 0; s < rateLaw.numModifiers(); s++) {
                const int &species_idx = species_indices[species_indices.size() - 1];
                modifiers.push_back(species_idx);
                species_indices.resize(species_indices.size() - 1);
            }
            assert(species_indices.empty());

            //todo put a max fail break in that breaks the recursion

            // first sort so we are guarenteed to have same ordering each time we do comparison
            std::sort(substrates.begin(), substrates.end());
            std::sort(products.begin(), products.end());
            std::sort(modifiers.begin(), modifiers.end());

            bool substrates_in_reaction = false;
            bool products_in_reaction = false;
            bool modifiers_in_reaction = false;
            if (std::find(reactions.substrates.begin(), reactions.substrates.end(), substrates) !=
                reactions.substrates.end())
                substrates_in_reaction = true;

            if (std::find(reactions.products.begin(), reactions.products.end(), products) != reactions.products.end())
                products_in_reaction = true;

            if (std::find(reactions.modifiers.begin(), reactions.modifiers.end(), modifiers) !=
                reactions.modifiers.end())
                modifiers_in_reaction = true;

            if (substrates_in_reaction && products_in_reaction && modifiers_in_reaction) {
                // otherwise a like reaction is already defined
                reaction_number--; // subtract 1 from reaction number to keep reaction count linear.
                recursion_count++;
                if (recursion_count == max_recursion_) {
                    LOG("Max recursion of " << max_recursion_ << " has been reached.");
                    break;
                }
            } else {
                // we only add to model when these three are false
                reactions.ids[reaction_number] = reaction_name.str();
                reaction_name.str("");// clear the stream
                reactions.rate_laws[reaction_number] = rateLaw;
                reactions.substrates[reaction_number] = substrates;
                reactions.products[reaction_number] = products;
                reactions.modifiers[reaction_number] = modifiers;
                recursion_count = 0; // reset the recursion count since we successfully added a reaction.
            }
        }
        return reactions;
    }

//    RandomNetworkGenerator* RandomNetworkFactory(const RandomNetworkGeneratorOptions &options, EvoRandomNetworkGenerator which){
//
//        switch(which){
//            case (EVO_NAIVE_RANDOM_NETWORK_GENERATOR):{
//                NaiveRandomNetworkGenerator generator(options);
//                return std::move(generator);
//            }
//            case EVO_UNIQUE_REACTIONS_RANDOM_NETWORK_GENERATOR:
//                break;
//        }
//    }

}// namespace evo




































