//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#if !defined(LCM_ElastoViscoplasticModel_hpp)
#define LCM_ElastoViscoplasticModel_hpp

#include "Phalanx_ConfigDefs.hpp"
#include "Phalanx_Evaluator_WithBaseImpl.hpp"
#include "Phalanx_Evaluator_Derived.hpp"
#include "Phalanx_MDField.hpp"
#include "Albany_Layouts.hpp"
#include "LCM/models/ConstitutiveModel.hpp"

namespace LCM
{

//! \brief Elasto Viscoplastic Constitutive Model
template<typename EvalT, typename Traits>
class ElastoViscoplasticModel: public LCM::ConstitutiveModel<EvalT, Traits>
{
public:

  typedef typename EvalT::ScalarT ScalarT;
  typedef typename EvalT::MeshScalarT MeshScalarT;
  typedef typename Sacado::Fad::DFad<ScalarT> Fad;

  using ConstitutiveModel<EvalT, Traits>::num_dims_;
  using ConstitutiveModel<EvalT, Traits>::num_pts_;
  using ConstitutiveModel<EvalT, Traits>::field_name_map_;

  // optional temperature support
  using ConstitutiveModel<EvalT, Traits>::have_temperature_;
  using ConstitutiveModel<EvalT, Traits>::expansion_coeff_;
  using ConstitutiveModel<EvalT, Traits>::ref_temperature_;
  using ConstitutiveModel<EvalT, Traits>::heat_capacity_;
  using ConstitutiveModel<EvalT, Traits>::density_;
  using ConstitutiveModel<EvalT, Traits>::temperature_;

  ///
  /// Constructor
  ///
  ElastoViscoplasticModel(Teuchos::ParameterList* p,
      const Teuchos::RCP<Albany::Layouts>& dl);

  ///
  /// Virtual Denstructor
  ///
  virtual
  ~ElastoViscoplasticModel()
  {};

  ///
  /// Method to compute the state (e.g. energy, stress, tangent)
  ///
  virtual
  void
  computeState(typename Traits::EvalData workset,
      std::map<std::string, Teuchos::RCP<PHX::MDField<ScalarT> > > dep_fields,
      std::map<std::string, Teuchos::RCP<PHX::MDField<ScalarT> > > eval_fields);

private:

  ///
  /// Private to prohibit copying
  ///
  ElastoViscoplasticModel(const ElastoViscoplasticModel&);

  ///
  /// Private to prohibit copying
  ///
  ElastoViscoplasticModel& operator=(const ElastoViscoplasticModel&);

  ///
  /// Initial Void Volume
  ///
  RealType f0_;

  ///
  /// Shear Damage Parameter
  ///
  RealType kw_;

  ///
  /// Void Nucleation Parameters
  ///
  RealType eN_, sN_, fN_;

  ///
  /// Critical Void Parameters
  ///
  RealType fc_, ff_;

  ///
  /// Yield Parameters
  ///
  RealType q1_, q2_, q3_;

  ///
  /// Compute Yield Function
  ///
  template<typename T>
  T
  compute_fstar(T f, RealType fc, RealType ff, RealType q1);

};
}

#endif
