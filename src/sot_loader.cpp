/*
 * Copyright 2011,
 * Olivier Stasse,
 *
 * CNRS
 *
 * This file is part of sot-core.
 * sot-core is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 * sot-core is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.  You should
 * have received a copy of the GNU Lesser General Public License along
 * with sot-core.  If not, see <http://www.gnu.org/licenses/>.
 */
/* -------------------------------------------------------------------------- */
/* --- INCLUDES ------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

#include "sot_loader.hh"

// POSIX.1-2001
#include <dlfcn.h>

using namespace std;
using namespace dynamicgraph::sot; 
namespace po = boost::program_options;

SotLoader::SotLoader():
  dynamic_graph_stopped_(true),
  sensorsIn_ (),
  controlValues_ (),
  angleEncoder_ (),
  angleControl_ (),
  forces_ (),
  torques_ (),
  baseAtt_ (),
  accelerometer_ (3),
  gyrometer_ (3)
{
  readSotVectorStateParam();
  initPublication();
}

int SotLoader::initPublication()
{
  ros::NodeHandle n;


  // Prepare message to be published
  joint_pub_ = n.advertise<sensor_msgs::JointState>("joint_states", 1);

  return 0;
}

int SotLoader::readSotVectorStateParam()
{
  std::map<std::string,int> from_state_name_to_state_vector;
  std::map<std::string,std::string> from_parallel_name_to_state_vector_name;
  ros::NodeHandle n;

  if (!ros::param::has("/sot/state_vector_map"))
    {
      std::cerr<< " Read Sot Vector State Param " << std::endl;
      std::cerr<< " Can't read state_vector_map. Does not exist. "<<std::endl;
      return 1;
    }

  n.getParam("/sot/state_vector_map", stateVectorMap_);
  ROS_ASSERT(stateVectorMap_.getType() == XmlRpc::XmlRpcValue::TypeArray);
  nbOfJoints_ = stateVectorMap_.size();
  nbOfParallelJoints_ = 0;
  if (ros::param::has("/sot/joint_state_parallel"))
    {
      XmlRpc::XmlRpcValue joint_state_parallel;
      n.getParam("/sot/joint_state_parallel", joint_state_parallel);
      ROS_ASSERT(joint_state_parallel.getType() == XmlRpc::XmlRpcValue::TypeStruct);
      std::cout << "Type of joint_state_parallel:" << joint_state_parallel.getType() << std::endl;

      for(XmlRpc::XmlRpcValue::iterator it = joint_state_parallel.begin(); 
          it!= joint_state_parallel.end(); it++) 
        {
          XmlRpc::XmlRpcValue local_value=it->second;
          std::string final_expression, map_expression = static_cast<string>(local_value); 
          double final_coefficient = 1.0;
          // deal with sign 
          if (map_expression[0]=='-')
            {
              final_expression = map_expression.substr(1,map_expression.size()-1);
              final_coefficient = -1.0;
            }
          else 
            final_expression = map_expression;

          std::cout <<it->first.c_str() << ": " << final_coefficient << std::endl;
          from_parallel_name_to_state_vector_name[it->first.c_str()] = final_expression;
          coefficient_parallel_joints_.push_back(final_coefficient);

        }
      nbOfParallelJoints_ = from_parallel_name_to_state_vector_name.size();
    }

  // Prepare joint_state according to robot description.
  joint_state_.name.resize(nbOfJoints_+nbOfParallelJoints_);
  joint_state_.position.resize(nbOfJoints_+nbOfParallelJoints_);
  
  // Fill in the name of the joints from the state vector.
  // and build local map from state name to state vector
  for (int32_t i = 0; i < stateVectorMap_.size(); ++i) 
   {
     joint_state_.name[i]= static_cast<string>(stateVectorMap_[i]);

     from_state_name_to_state_vector[joint_state_.name[i]] = i;
   }

  // Fill in the name of the joints from the parallel joint vector.
  // and build map from parallel name to state vector
  int i=0;
  parallel_joints_to_state_vector_.resize(nbOfParallelJoints_);
  for (std::map<std::string,std::string>::iterator  it = from_parallel_name_to_state_vector_name.begin();
       it != from_parallel_name_to_state_vector_name.end();
       it++,i++)
    {
      joint_state_.name[i+nbOfJoints_]=it->first.c_str();
      parallel_joints_to_state_vector_[i] = from_state_name_to_state_vector[it->second];
    }

  angleEncoder_.resize(nbOfJoints_);
  
  return 0;
}


int SotLoader::parseOptions(int argc, char *argv[])
{
  po::options_description desc("Allowed options");
  desc.add_options()
    ("help", "produce help message")
    ("input-file", po::value<string>(), "library to load")
    ;
    
  
  po::store(po::parse_command_line(argc, argv, desc), vm_);
  po::notify(vm_);    
  
  if (vm_.count("help")) {
    cout << desc << "\n";
    return -1;
  }
  if (!vm_.count("input-file")) {
    cout << "No filename specified\n";
    return -1;
  }
  else
    dynamicLibraryName_= vm_["input-file"].as< string >();
  
  Initialization();
  return 0;
}

void SotLoader::Initialization()
{
 
 // Load the SotRobotBipedController library.
  void * SotRobotControllerLibrary = dlopen( dynamicLibraryName_.c_str(),
                                             RTLD_LAZY | RTLD_GLOBAL );
  if (!SotRobotControllerLibrary) {
    std::cerr << "Cannot load library: " << dlerror() << '\n';
    return ;
  }
  
  // reset errors
  dlerror();
  
  // Load the symbols.
  createSotExternalInterface_t * createSot =
    reinterpret_cast<createSotExternalInterface_t *> 
    (reinterpret_cast<long> 
     (dlsym(SotRobotControllerLibrary, 
	    "createSotExternalInterface")));
  const char* dlsym_error = dlerror();
  if (dlsym_error) {
    std::cerr << "Cannot load symbol create: " << dlsym_error << '\n';
    return ;
  }
  
  // Create robot-controller
  sotController_ = createSot();
  cout <<"Went out from Initialization." << endl;
}

void 
SotLoader::fillSensors(map<string,dgs::SensorValues> & sensorsIn)
{
  
  // Update joint values.w
  sensorsIn["joints"].setName("angle");
  for(unsigned int i=0;i<angleControl_.size();i++)
    angleEncoder_[i] = angleControl_[i];
  sensorsIn["joints"].setValues(angleEncoder_);
  
}

void 
SotLoader::readControl(map<string,dgs::ControlValues> &controlValues)
{

  
  // Update joint values.
  angleControl_ = controlValues["joints"].getValues();

  //Debug
  std::map<std::string,dgs::ControlValues>::iterator it = controlValues.begin();
  sotDEBUG (30)<<"ControlValues to be broadcasted:"<<std::endl;
  for(;it!=controlValues.end(); it++){
    sotDEBUG (30)<<it->first<<":";
    std::vector<double> ctrlValues_ = it->second.getValues();
    std::vector<double>::iterator it_d = ctrlValues_.begin();
    for(;it_d!=ctrlValues_.end();it_d++) sotDEBUG (30)<<*it_d<<" ";
    sotDEBUG (30)<<std::endl;
  }
  sotDEBUG (30)<<"End ControlValues"<<std::endl;


  // Check if the size if coherent with the robot description.
  if (angleControl_.size()!=(unsigned int)nbOfJoints_)
    {
      std::cerr << " angleControl_"<<angleControl_.size()
		<< " and nbOfJoints"<<(unsigned int)nbOfJoints_
		<< " are different !"
                << std::endl;
      exit(-1);
    }
  // Publish the data.
  joint_state_.header.stamp = ros::Time::now();  
  for(int i=0;i<nbOfJoints_;i++)
    {
      joint_state_.position[i] = angleControl_[i];
    }
  for(unsigned int i=0;i<parallel_joints_to_state_vector_.size();i++)
    {
      joint_state_.position[i+nbOfJoints_] = 
        coefficient_parallel_joints_[i]*angleControl_[parallel_joints_to_state_vector_[i]];
    }

  joint_pub_.publish(joint_state_);

  //Publish robot pose
  //get the robot pose values
  std::vector<double> poseValue_ = controlValues["baseff"].getValues();

  freeFlyerPose_.setOrigin(tf::Vector3(poseValue_[0],
				       poseValue_[1],
				       poseValue_[2]));
  tf::Quaternion poseQ_(poseValue_[4],
			poseValue_[5],
			poseValue_[6],
			poseValue_[3]);
  freeFlyerPose_.setRotation(poseQ_);
  //Publish
  freeFlyerPublisher_.sendTransform(tf::StampedTransform(freeFlyerPose_,
							 ros::Time::now(),
							 "odom",
							 "base_link"));
  
}

void SotLoader::setup()
{
  fillSensors(sensorsIn_);
  sotController_->setupSetSensors(sensorsIn_);
  sotController_->getControl(controlValues_); 
  readControl(controlValues_); 
}

void SotLoader::oneIteration()
{
  fillSensors(sensorsIn_);
  try 
    {
      sotController_->nominalSetSensors(sensorsIn_);
      sotController_->getControl(controlValues_);
    } 
  catch(std::exception &e) { throw e;} 
  
  readControl(controlValues_);
}


bool SotLoader::start_dg(std_srvs::Empty::Request& , 
                         std_srvs::Empty::Response& )
{
  dynamic_graph_stopped_=false;    
  return true;
}

bool SotLoader::stop_dg(std_srvs::Empty::Request& , 
                         std_srvs::Empty::Response& )
{
  dynamic_graph_stopped_ = true;
  return true;
}

