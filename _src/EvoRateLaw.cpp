//
// Created by Ciaran on 08/09/2020.
//

#include "sbml/SBMLTypes.h"
#include <regex>

#include "evo/EvoRateLaw.h"
#include "evo/evo_error.h"
#include "evo/logger.h"

namespace evo {

    EvoRateLaw::EvoRateLaw(std::string name, const std::string &rate_law_string, RoleMap roles)
            : name_(std::move(name)),
              rate_law_string_(rate_law_string),
              rate_law_(libsbml::SBML_parseFormula(rate_law_string.c_str())),
              roles_(std::move(roles)) {
        unpackRateLaw();
    }

    void EvoRateLaw::setName(const std::string &name) {
        name_ = name;
    }

    void EvoRateLaw::setRateLaw(libsbml::ASTNode *rateLaw) {
        rate_law_ = rateLaw;
    }

    const std::string &EvoRateLaw::getRateLawString() const {
        return rate_law_string_;
    }

    void EvoRateLaw::setRateLawString(const std::string &rateLawString) {
        rate_law_string_ = rateLawString;
    }

    void EvoRateLaw::setRoles(const RoleMap &roles) {
        roles_ = roles;
    }

    void EvoRateLaw::setRateLawElements(const std::set<std::string> &rateLawElements) {
        rate_law_elements_ = rateLawElements;
    }

    void EvoRateLaw::unpackRateLaw(std::vector<libsbml::ASTNode *> rate_law_terms) {
        /*
         * When rate_law_terms is empty, the recursive function is being called for the first time
         * so we need to populate the rate_law_terms with the parent equation that we
         * want to get variables from.
         */
        if (rate_law_terms.empty()) {
            rate_law_terms.push_back(rate_law_);
        }

        /*
         * We iterate over the vector of libsbml::ASTNode* pointers; if we encounter
         * a leaf node (aka no children) we add the string representation of the node to
         * the rate_law_elements_ vector. When we encounter a branch node (i.e. 1 or more children)
         * we construct a new rate_law_terms vector from the children of each element
         * of the current rate_law_terms vector.
         */
        for (auto &rate_law_term : rate_law_terms) {
            if (rate_law_term->getNumChildren() == 0) {
//                std::string elementary_term_string = libsbml::SBML_formulaToString(rate_law_term);
                char *s = libsbml::SBML_formulaToString(rate_law_term);
                std::string elementary_term_string(s);
                free(s);
                rate_law_elements_.insert(elementary_term_string);
            } else {
                std::vector<libsbml::ASTNode *> new_rate_law_terms;
                for (int i = 0; i < rate_law_term->getNumChildren(); i++) {
                    auto node_type = rate_law_term->getChild(i)->getType();
                    // exclude integers or doubles. These are parameters, like K
                    if (node_type == libsbml::AST_INTEGER ||
                        node_type == libsbml::AST_REAL ||
                        node_type == libsbml::AST_REAL_E) {
                        continue;
                    }
                    new_rate_law_terms.push_back(rate_law_term->getChild(i));
                }
                unpackRateLaw(new_rate_law_terms);
            }
        }
    }

    const std::set<std::string> &EvoRateLaw::getRateLawElements() const {
        return rate_law_elements_;
    }

    const std::string &EvoRateLaw::getName() const {
        return name_;
    }

    libsbml::ASTNode *EvoRateLaw::getRateLaw() const {
        return rate_law_;
    }

    const RoleMap &EvoRateLaw::getRoles() const {
        return roles_;
    }

    int EvoRateLaw::countRoles(RoleType role) const {
        // count number of parameters
        int count = 0;
        for (auto &it : getRoles()) {
            if (it.second == role)
                count += 1;
        }
        assert(count < getRoles().size());
        return count;
    }

    int EvoRateLaw::numParameters() const {
        int count = countRoles(RoleType::EVO_PARAMETER);
        if (count == 0) {
            LOGIC_ERROR << "Counted 0 kinetic parameters in your rate law. This"
                           "is highly unusual and probably wrong.";
        }

        return count;
    }

    int EvoRateLaw::numSubstrates() const {
        return countRoles(EVO_SUBSTRATE);
    }

    int EvoRateLaw::numProducts() const {
        return countRoles(EVO_PRODUCT);
    }

    int EvoRateLaw::numModifiers() const {
        return countRoles(EVO_MODIFIER);
    }

    RateLaws massActionRateLaws() {
        RateLaws rateLaws;
        rateLaws["uni-uni"] = EvoRateLaw("uni-uni", "k*A",
                                         RoleMap({
                                                         {"k", EVO_PARAMETER},
                                                         {"A", EVO_SUBSTRATE},
                                                         {"B", EVO_PRODUCT},
                                                 }));

        rateLaws["uni-bi"] = EvoRateLaw("uni-bi", "k*A",
                                        RoleMap({
                                                        {"k", EVO_PARAMETER},
                                                        {"A", EVO_SUBSTRATE},
                                                        {"B", EVO_PRODUCT},
                                                        {"C", EVO_PRODUCT},
                                                }));

        rateLaws["bi-uni"] = EvoRateLaw("bi-uni", "k*A*B",
                                        RoleMap({
                                                        {"k", EVO_PARAMETER},
                                                        {"A", EVO_SUBSTRATE},
                                                        {"B", EVO_SUBSTRATE},
                                                        {"C", EVO_PRODUCT},
                                                }));
        rateLaws["bi-bi"] = EvoRateLaw("bi-bi", "k*A*B",
                                       RoleMap({
                                                       {"k", EVO_PARAMETER},
                                                       {"A", EVO_SUBSTRATE},
                                                       {"B", EVO_SUBSTRATE},
                                                       {"C", EVO_PRODUCT},
                                                       {"D", EVO_PRODUCT},
                                               }));
        return rateLaws;
    }

}// namespace evo
