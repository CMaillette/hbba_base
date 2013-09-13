#include <iw_translator/iw_translator.hpp>
#include <iw_translator/strategy_parser.hpp>
#include <hbba_msgs/Intention.h>

using namespace iw_translator;

namespace 
{
    // Use an empty string for unknown/unmatched desires:
    static const std::string EMPTY("");

    const std::string& desireIdFromType(
        const hbba_msgs::DesiresSet& desires_set, 
        const std::string& type)
    {

        // Return the first one found, or unknown.

        typedef std::vector<hbba_msgs::Desire> Desires;
        typedef Desires::const_iterator It;
        const Desires& desires = desires_set.desires;
        for (It i = desires.begin(); i != desires.end(); ++i) {
            if (i->type == type) {
                return i->id;
            }
        }

        return EMPTY;
    }
}

IWTranslator::IWTranslator(ros::NodeHandle& n, ros::NodeHandle& np)
{
    if (!np.hasParam("strategies")) {
        ROS_ERROR(
            "No strategies defined for IWTranslator, will not be initialized.");
        return;
    } 
    XmlRpc::XmlRpcValue strats_def;
    np.getParam("strategies", strats_def);
    if (!StrategyParser::parseArray(strats_def, strats_)) {
        ROS_ERROR("Cannot properly parse strategies, will not run.");
        return;
    }

    // Note: An empty resource caps vector is valid, just not as useful.
    std::vector<hbba_msgs::ResourceUsage> res_caps;
    if (np.hasParam("res_caps")) {
        XmlRpc::XmlRpcValue res_caps_def;
        np.getParam("res_caps", res_caps_def);
        StrategyParser::parseCosts(res_caps_def, res_caps);
    }

    solver_model_.reset(new SolverModel(strats_, res_caps));

    sub_desires_ = n.subscribe(
        "desires_set", 
        10, 
        &IWTranslator::desiresCB, 
        this);

    pub_intention_ = n.advertise<hbba_msgs::Intention>(
        "intention",
        10,
        true); // Always latch.
}

void IWTranslator::desiresCB(const hbba_msgs::DesiresSet::ConstPtr& msg)
{
    Vector g;
    if (!solver_model_->convertDesires(*msg, g)) {
        ROS_WARN("Class with unknown desires will be ignored.");
    }

    Solver solver(*solver_model_, g);

    ActivationVector a;
    if (solver.result(a)) {
        ROS_DEBUG("Solving succeeded.");
        hbba_msgs::Intention out;
        for (size_t i = 0; i < a.size(); ++i) {
            ROS_DEBUG(
                "Strategy %s activation: %s", 
                strats_[i].id.c_str(), 
                a[i] ? "true":"false");

            const std::string& strat_id = strats_[i].id;
            const std::string& des_type = a[i] ?
                strats_[i].utility.id            : EMPTY;
            const std::string& des_id   = a[i] ? 
                desireIdFromType(*msg, des_type) : EMPTY;

            out.strategies.push_back(   strat_id);
            out.desires.push_back(      des_id);
            out.desire_types.push_back( des_type);
            out.enabled.push_back(      a[i]);
        }

        out.stamp = ros::Time::now();
        pub_intention_.publish(out);
    };
}
