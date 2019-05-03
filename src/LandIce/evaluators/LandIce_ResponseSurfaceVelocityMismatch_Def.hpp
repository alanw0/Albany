//*****************************************************************//
//    Albany 3.0:  Copyright 2016 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#include <fstream>
#include "Teuchos_TestForException.hpp"
#include "Phalanx_DataLayout.hpp"
#include "Teuchos_CommHelpers.hpp"
#include "PHAL_Utilities.hpp"

#include "Albany_GeneralPurposeFieldsNames.hpp"
#include "LandIce_ResponseSurfaceVelocityMismatch.hpp"

template<typename EvalT, typename Traits>
LandIce::ResponseSurfaceVelocityMismatch<EvalT, Traits>::
ResponseSurfaceVelocityMismatch(Teuchos::ParameterList& p, const Teuchos::RCP<Albany::Layouts>& dl)
{
  // get and validate Response parameter list
  Teuchos::ParameterList* plist = p.get<Teuchos::ParameterList*>("Parameter List");
  Teuchos::RCP<const Teuchos::ParameterList> reflist = this->getValidResponseParameters();
  plist->validateParameters(*reflist, 0);

  Teuchos::RCP<Teuchos::ParameterList> paramList = p.get<Teuchos::RCP<Teuchos::ParameterList> >("Parameters From Problem");
  Teuchos::RCP<ParamLib> paramLib = paramList->get< Teuchos::RCP<ParamLib> > ("Parameter Library");
  scaling = plist->get<double>("Scaling Coefficient", 1.0);
  alpha = plist->get<double>("Regularization Coefficient", 0.0);
  asinh_scaling = plist->get<double>("Asinh Scaling", 10.0);
  alpha_stiffening = plist->get<double>("Regularization Coefficient Stiffening", 0.0);

  scalarRMS = paramList->get<bool>("Scalar RMS", false);

  const std::string& velocity_name           = paramList->get<std::string>("Surface Velocity Side QP Variable Name");
  const std::string& obs_velocity_name       = paramList->get<std::string>("Observed Surface Velocity Side QP Variable Name");
  const std::string& obs_velocityRMS_name    = paramList->get<std::string>("Observed Surface Velocity RMS Side QP Variable Name");
  const std::string& w_measure_surface_name  = paramList->get<std::string>("Weighted Measure Surface Name");
  //const std::string& metric_surface_name     = paramList->get<std::string>("Metric Surface Name");

  surfaceSideName = paramList->get<std::string> ("Surface Side Name");
  TEUCHOS_TEST_FOR_EXCEPTION (dl->side_layouts.find(surfaceSideName)==dl->side_layouts.end(), std::runtime_error,
                              "Error! Surface side data layout not found.\n");

  Teuchos::RCP<Albany::Layouts> dl_surface = dl->side_layouts.at(surfaceSideName);

  velocity            = decltype(velocity)(velocity_name, dl_surface->qp_vector);
  observedVelocity    = decltype(observedVelocity)(obs_velocity_name, dl_surface->qp_vector);
  w_measure_surface   = decltype(w_measure_surface)(w_measure_surface_name, dl_surface->qp_scalar);
  if(scalarRMS)
    observedVelocityMagnitudeRMS = decltype(observedVelocityMagnitudeRMS)(obs_velocityRMS_name, dl_surface->qp_scalar);
  else
    observedVelocityRMS = decltype(observedVelocityRMS)(obs_velocityRMS_name, dl_surface->qp_vector);

  //metric_surface      = decltype(metric_surface)(metric_surface_name, dl_surface->qp_tensor);

  // Get Dimensions
  numSideNodes  = dl_surface->node_scalar->extent(2);
  numSideDims   = dl_surface->node_gradient->extent(3);
  numSurfaceQPs = dl_surface->qp_scalar->extent(2);

  // add dependent fields
  this->addDependentField(velocity);
  this->addDependentField(observedVelocity);
  this->addDependentField(w_measure_surface);
  if(scalarRMS)
    this->addDependentField(observedVelocityMagnitudeRMS);
  else
    this->addDependentField(observedVelocityRMS);

  //this->addDependentField(metric_surface);

  if (alpha!=0)
  {
    beta_reg_params = *paramList->get<std::vector<Teuchos::RCP<Teuchos::ParameterList>>*>("Basal Regularization Params");

    for (auto pl : beta_reg_params) {
      // Setting up the fields required by the regularizations
      std::string ssName = paramList->get<std::string> ("Basal Side Name");

      TEUCHOS_TEST_FOR_EXCEPTION (dl->side_layouts.find(ssName)==dl->side_layouts.end(), std::runtime_error,
                                  "Error! Basal side data layout not found.\n");
      Teuchos::RCP<Albany::Layouts> dl_basal = dl->side_layouts.at(ssName);

      const std::string& grad_beta_name       = paramList->get<std::string>("Basal Friction Coefficient Name") + "_" + ssName + " Gradient";
      const std::string& w_measure_basal_name = Albany::weighted_measure_name + " " + ssName;
      const std::string& metric_basal_name    = Albany::metric_name + " " + ssName;

      grad_beta_vec.emplace_back(grad_beta_name, dl_basal->qp_gradient);
      w_measure_beta_vec.emplace_back(w_measure_basal_name, dl_basal->qp_scalar);
      metric_beta_vec.emplace_back(metric_basal_name, dl_basal->qp_tensor);

      numBasalQPs = dl_basal->qp_scalar->extent(2);

      this->addDependentField(w_measure_beta_vec.back());
      this->addDependentField(metric_beta_vec.back());
      this->addDependentField(grad_beta_vec.back());
    }
  }

  if (alpha_stiffening!=0)
  {
    // Setting up the fields required by the regularizations
    basalSideName = paramList->get<std::string> ("Basal Side Name");

    TEUCHOS_TEST_FOR_EXCEPTION (dl->side_layouts.find(basalSideName)==dl->side_layouts.end(), std::runtime_error,
                                "Error! Basal side data layout not found.\n");
    Teuchos::RCP<Albany::Layouts> dl_basal = dl->side_layouts.at(basalSideName);

    const std::string& stiffening_name      = paramList->get<std::string>("Stiffening Factor Name");
    const std::string& grad_stiffening_name = paramList->get<std::string>("Stiffening Factor Gradient Name");
    const std::string& w_measure_basal_name = paramList->get<std::string>("Weighted Measure Basal Name");
    const std::string& metric_basal_name    = paramList->get<std::string>("Metric Basal Name");

    stiffening      = decltype(stiffening)(stiffening_name, dl_basal->qp_scalar);
    grad_stiffening = decltype(grad_stiffening)(grad_stiffening_name, dl_basal->qp_gradient);
    w_measure_basal = decltype(w_measure_basal)(w_measure_basal_name, dl_basal->qp_scalar);
    metric_basal    = decltype(metric_basal)(metric_basal_name, dl_basal->qp_tensor);

    numBasalQPs = dl_basal->qp_scalar->extent(2);

    this->addDependentField(w_measure_basal);
    this->addDependentField(metric_basal);
    this->addDependentField(grad_stiffening);
    this->addDependentField(stiffening);
  }

  this->setName("Response surface_velocity Mismatch" + PHX::typeAsString<EvalT>());

  using PHX::MDALayout;

  // Setup scatter evaluator
  p.set("Stand-alone Evaluator", false);
  std::string local_response_name = "Local Response surface_velocity Mismatch";
  std::string global_response_name = "Global Response surface_velocity Mismatch";
  int worksetSize = dl->qp_scalar->extent(0);
  int responseSize = 1;
  Teuchos::RCP<PHX::DataLayout> local_response_layout = Teuchos::rcp(new MDALayout<Cell, Dim>(worksetSize, responseSize));
  Teuchos::RCP<PHX::DataLayout> global_response_layout = Teuchos::rcp(new MDALayout<Dim>(responseSize));
  PHX::Tag<ScalarT> local_response_tag(local_response_name, local_response_layout);
  PHX::Tag<ScalarT> global_response_tag(global_response_name, global_response_layout);
  p.set("Local Response Field Tag", local_response_tag);
  p.set("Global Response Field Tag", global_response_tag);
  PHAL::SeparableScatterScalarResponseWithExtrudedParams<EvalT, Traits>::setup(p, dl);
}

// **********************************************************************
template<typename EvalT, typename Traits>
void LandIce::ResponseSurfaceVelocityMismatch<EvalT, Traits>::
postRegistrationSetup(typename Traits::SetupData d, PHX::FieldManager<Traits>& fm)
{
  PHAL::SeparableScatterScalarResponseWithExtrudedParams<EvalT, Traits>::postRegistrationSetup(d, fm);
  d.fill_field_dependencies(this->dependentFields(),this->evaluatedFields());
}

// **********************************************************************
template<typename EvalT, typename Traits>
void LandIce::ResponseSurfaceVelocityMismatch<EvalT, Traits>::preEvaluate(typename Traits::PreEvalData workset)
{
  PHAL::set(this->global_response_eval, 0.0);

  p_resp = p_reg = p_reg_stiffening =0;

  // Do global initialization
  PHAL::SeparableScatterScalarResponseWithExtrudedParams<EvalT, Traits>::preEvaluate(workset);
}

// **********************************************************************
template<typename EvalT, typename Traits>
void LandIce::ResponseSurfaceVelocityMismatch<EvalT, Traits>::evaluateFields(typename Traits::EvalData workset)
{
  TEUCHOS_TEST_FOR_EXCEPTION (workset.sideSets==Teuchos::null, std::logic_error,
                              "Side sets defined in input file but not properly specified on the mesh" << std::endl);

  // Zero out local response
  PHAL::set(this->local_response_eval, 0.0);

  // ----------------- Surface side ---------------- //

  if (workset.sideSets->find(surfaceSideName) != workset.sideSets->end())
  {
    const std::vector<Albany::SideStruct>& sideSet = workset.sideSets->at(surfaceSideName);
    for (auto const& it_side : sideSet)
    {
      // Get the local data of side and cell
      const int cell = it_side.elem_LID;
      const int side = it_side.side_local_id;


      ScalarT t = 0;
      ScalarT data = 0;
      if(scalarRMS)
        for (int qp=0; qp<numSurfaceQPs; ++qp)
        {
          ScalarT diff2 = std::pow(velocity(cell, side, qp, 0)  - observedVelocity (cell, side, qp, 0),2) 
                               + std::pow(velocity(cell, side, qp, 1)  - observedVelocity (cell, side, qp, 1),2);
          ScalarT weightedDiff = std::sqrt(diff2)/observedVelocityMagnitudeRMS(cell, side, qp);
          ScalarT weightedDiff2 = std::pow(asinh(weightedDiff/ asinh_scaling)*asinh_scaling,2);
          t += weightedDiff2 * w_measure_surface(cell,side,qp);
        }
      else
        for (int qp=0; qp<numSurfaceQPs; ++qp)
        {
          ParamScalarT refVel0 = asinh(observedVelocity (cell, side, qp, 0) / observedVelocityRMS(cell, side, qp, 0) / asinh_scaling);
          ParamScalarT refVel1 = asinh(observedVelocity (cell, side, qp, 1) / observedVelocityRMS(cell, side, qp, 1) / asinh_scaling);
          ScalarT vel0 = asinh(velocity(cell, side, qp, 0) / observedVelocityRMS(cell, side, qp, 0) / asinh_scaling);
          ScalarT vel1 = asinh(velocity(cell, side, qp, 1) / observedVelocityRMS(cell, side, qp, 1) / asinh_scaling);
          ScalarT diff0 = refVel0 - vel0;
          ScalarT diff1 = refVel1 - vel1;
          data = diff0 * diff0
               + diff1 * diff1;
          //data = diff0 * metric_surface(cell,side,qp,0,0) * diff0
          //     + diff0 * metric_surface(cell,side,qp,0,1) * diff1
          //     + diff1 * metric_surface(cell,side,qp,1,0) * diff0;
          //     + diff1 * metric_surface(cell,side,qp,1,1) * diff1;
          data *= asinh_scaling * asinh_scaling;
          t += data * w_measure_surface(cell,side,qp);
        }

      this->local_response_eval(cell, 0) += t*scaling;
      this->global_response_eval(0) += t*scaling;
      p_resp += t*scaling;
    }
  }

  // --------------- Regularization term on the basal side ----------------- //

  if (alpha!=0) {
    for (size_t i=0; i<beta_reg_params.size(); ++i) {
      Teuchos::RCP<Teuchos::ParameterList> pl = beta_reg_params[i];
      std::string ssName = pl->get<std::string>("Side Set Name","");

      auto grad_beta = grad_beta_vec[i];
      auto metric = metric_beta_vec[i];
      auto w_measure = w_measure_beta_vec[i];
      if (workset.sideSets->find(ssName) != workset.sideSets->end()) {
        const std::vector<Albany::SideStruct>& sideSet = workset.sideSets->at(ssName);
        for (auto const& it_side : sideSet)
        {
          // Get the local data of side and cell
          const int cell = it_side.elem_LID;
          const int side = it_side.side_local_id;
          ScalarT t = 0;
          for (int qp=0; qp<numBasalQPs; ++qp)
          {
            ScalarT sum=0;
            for (int idim=0; idim<numSideDims; ++idim)
              for (int jdim=0; jdim<numSideDims; ++jdim)
                sum += grad_beta(cell,side,qp,idim)*metric(cell,side,qp,idim,jdim)*grad_beta(cell,side,qp,jdim);

            t += sum * w_measure(cell,side,qp);
          }
          this->local_response_eval(cell, 0) += t*scaling*alpha;//*50.0;
          this->global_response_eval(0) += t*scaling*alpha;//*50.0;
          p_reg += t*scaling*alpha;
        }
      }
    }
  }

  if (workset.sideSets->find(basalSideName) != workset.sideSets->end() && alpha_stiffening!=0)
  {
    const std::vector<Albany::SideStruct>& sideSet = workset.sideSets->at(basalSideName);
    for (auto const& it_side : sideSet)
    {
      // Get the local data of side and cell
      const int cell = it_side.elem_LID;
      const int side = it_side.side_local_id;
      ScalarT t = 0;
      for (int qp=0; qp<numBasalQPs; ++qp)
      {
        ScalarT sum = stiffening(cell,side,qp)*stiffening(cell,side,qp);
        for (int idim=0; idim<numSideDims; ++idim)
          for (int jdim=0; jdim<numSideDims; ++jdim)
            sum += grad_stiffening(cell,side,qp,idim)*metric_basal(cell,side,qp,idim,jdim)*grad_stiffening(cell,side,qp,jdim);

        t += sum * w_measure_basal(cell,side,qp);
      }
      this->local_response_eval(cell, 0) += t*scaling*alpha_stiffening;//*50.0;
      this->global_response_eval(0) += t*scaling*alpha_stiffening;//*50.0;
      p_reg_stiffening += t*scaling*alpha_stiffening;
    }
  }

  // Do any local-scattering necessary
  PHAL::SeparableScatterScalarResponseWithExtrudedParams<EvalT, Traits>::evaluateFields(workset);
}

// **********************************************************************
template<typename EvalT, typename Traits>
void LandIce::ResponseSurfaceVelocityMismatch<EvalT, Traits>::postEvaluate(typename Traits::PostEvalData workset) {

  //amb Deal with op[], pointers, and reduceAll.
  PHAL::reduceAll<ScalarT>(*workset.comm, Teuchos::REDUCE_SUM,
                           this->global_response_eval);
  PHAL::reduceAll<ScalarT>(*workset.comm, Teuchos::REDUCE_SUM, p_resp);
  resp = p_resp;
  PHAL::reduceAll<ScalarT>(*workset.comm, Teuchos::REDUCE_SUM, p_reg);
  reg = p_reg;
  PHAL::reduceAll<ScalarT>(*workset.comm, Teuchos::REDUCE_SUM, p_reg_stiffening);
  reg_stiffening = p_reg_stiffening;

  if(workset.comm->getRank()   ==0)
    std::cout << "SV, resp: " << Sacado::ScalarValue<ScalarT>::eval(resp) << ", reg: " << Sacado::ScalarValue<ScalarT>::eval(reg) <<  ", reg_stiffening: " << Sacado::ScalarValue<ScalarT>::eval(reg_stiffening) <<std::endl;

  if (rank(*workset.comm) == 0) {
    std::ofstream ofile;
    ofile.open("velocity_mismatch");
    if (ofile.is_open(), std::ofstream::out | std::ofstream::trunc) {
      ofile <<  std::scientific << std::setprecision(15) << Sacado::ScalarValue<ScalarT>::eval(resp);
      ofile.close();
    }
    ofile.open("beta_regularization");
    if (ofile.is_open(), std::ofstream::out | std::ofstream::trunc) {
      ofile <<  std::scientific << std::setprecision(15) << Sacado::ScalarValue<ScalarT>::eval(reg);
      ofile.close();
    }
  }

  // Do global scattering
  PHAL::SeparableScatterScalarResponseWithExtrudedParams<EvalT, Traits>::postEvaluate(workset);
}

// **********************************************************************
template<typename EvalT, typename Traits>
Teuchos::RCP<const Teuchos::ParameterList> LandIce::ResponseSurfaceVelocityMismatch<EvalT, Traits>::getValidResponseParameters() const
{
  Teuchos::RCP<Teuchos::ParameterList> validPL = rcp(new Teuchos::ParameterList("Valid ResponseSurfaceVelocityMismatch Params"));
  Teuchos::RCP<const Teuchos::ParameterList> baseValidPL = PHAL::SeparableScatterScalarResponseWithExtrudedParams<EvalT, Traits>::getValidResponseParameters();
  validPL->setParameters(*baseValidPL);

  validPL->set<std::string>("Name", "", "Name of response function");
  validPL->set<std::string>("Field Name", "Solution", "Not used");
  validPL->set<double>("Regularization Coefficient", 1.0, "Regularization Coefficient");
  validPL->set<double>("Regularization Coefficient Stiffening", 1.0, "Regularization Coefficient Stiffening");
  validPL->set<double>("Scaling Coefficient", 1.0, "Coefficient that scales the response");
  validPL->set<double>("Asinh Scaling", 1.0, "Scaling s in asinh(s*x)/s. Used to penalize high values of velocity");
  validPL->set<int>("Cubature Degree", 3, "degree of cubature used to compute the velocity mismatch");
  validPL->set<int>("Phalanx Graph Visualization Detail", 0, "Make dot file to visualize phalanx graph");
  validPL->set<std::string>("Description", "", "Description of this response used by post processors");

  validPL->set<std::string> ("Basal Side Name", "", "Name of the side set correspongint to the ice-bedrock interface");
  validPL->set<std::string> ("Surface Side Name", "", "Name of the side set corresponding to the ice surface");

  return validPL;
}
// **********************************************************************

