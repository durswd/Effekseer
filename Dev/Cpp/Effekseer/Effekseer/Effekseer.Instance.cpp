﻿
//----------------------------------------------------------------------------------
//
//----------------------------------------------------------------------------------
#include "Effekseer.Manager.h"
#include "Effekseer.EffectNode.h"
#include "Effekseer.Instance.h"
#include "Effekseer.InstanceGroup.h"
#include "Effekseer.InstanceContainer.h"
#include "Effekseer.InstanceGlobal.h"
#include "Effekseer.ManagerImplemented.h"
#include "Effekseer.Effect.h"
#include "Effekseer.EffectImplemented.h"
#include "Effekseer.Model.h"

//----------------------------------------------------------------------------------
//
//----------------------------------------------------------------------------------
namespace Effekseer
{
//----------------------------------------------------------------------------------
//
//----------------------------------------------------------------------------------
Instance::Instance(Manager* pManager, EffectNode* pEffectNode, InstanceContainer* pContainer)
	: m_pManager(pManager)
	, m_pEffectNode((EffectNodeImplemented*)pEffectNode)
	, m_pContainer(pContainer)
	, m_headGroups(NULL)
	, m_pParent(NULL)
	, m_State(INSTANCE_STATE_ACTIVE)
	, m_LivedTime(0)
	, m_LivingTime(0)
	, uvTimeOffset(0)
	, m_GlobalMatrix43Calculated(false)
	, m_ParentMatrix43Calculated(false)
	, m_flexibleGeneratedChildrenCount(nullptr)
	, m_flexibleNextGenerationTime(nullptr)
	, m_stepTime(false)
	, m_sequenceNumber(0)
{
	m_generatedChildrenCount = m_fixedGeneratedChildrenCount;
	m_nextGenerationTime = m_fixedNextGenerationTime;
	
	ColorInheritance = Color(255, 255, 255, 255);
	ColorParent = Color(255, 255, 255, 255);

	InstanceGroup* group = NULL;

	for( int i = 0; i < m_pEffectNode->GetChildrenCount(); i++ )
	{
		InstanceContainer* pContainer = m_pContainer->GetChild( i );

		if( group != NULL )
		{
			group->NextUsedByInstance = pContainer->CreateGroup();
			group = group->NextUsedByInstance;
		}
		else
		{
			group = pContainer->CreateGroup();
			m_headGroups = group;
		}
	}
}

//----------------------------------------------------------------------------------
//
//----------------------------------------------------------------------------------
Instance::~Instance()
{
	assert( m_State != INSTANCE_STATE_ACTIVE );

	auto parameter = (EffectNodeImplemented*)m_pEffectNode;

	if (m_flexibleGeneratedChildrenCount != nullptr)
	{
		m_pManager->GetFreeFunc()(m_flexibleGeneratedChildrenCount, sizeof(int32_t) * parameter->GetChildrenCount());
	}

	if (m_flexibleNextGenerationTime != nullptr)
	{
		m_pManager->GetFreeFunc()(m_flexibleNextGenerationTime, sizeof(float) * parameter->GetChildrenCount());
	}
}

//----------------------------------------------------------------------------------
//
//----------------------------------------------------------------------------------
eInstanceState Instance::GetState() const
{
	return m_State;
}

//----------------------------------------------------------------------------------
//
//----------------------------------------------------------------------------------
const Matrix43& Instance::GetGlobalMatrix43() const
{
	return m_GlobalMatrix43;
}

//----------------------------------------------------------------------------------
//
//----------------------------------------------------------------------------------
void Instance::Initialize( Instance* parent, int32_t instanceNumber )
{
	assert(this->m_pContainer != nullptr);
	
	// Invalidate matrix
	m_GlobalMatrix43Calculated = false;
	m_ParentMatrix43Calculated = false;

	auto instanceGlobal = this->m_pContainer->GetRootInstance();

	auto parameter = (EffectNodeImplemented*) m_pEffectNode;

	// Extend array
	if (parameter->GetChildrenCount() >= ChildrenMax)
	{
		m_flexibleGeneratedChildrenCount = (int32_t*)(m_pManager->GetMallocFunc()(sizeof(int32_t) * parameter->GetChildrenCount()));
		m_flexibleNextGenerationTime = (float*)(m_pManager->GetMallocFunc()(sizeof(float) * parameter->GetChildrenCount()));

		m_generatedChildrenCount = m_flexibleGeneratedChildrenCount;
		m_nextGenerationTime = m_flexibleNextGenerationTime;
	}

	// 親の設定
	m_pParent = parent;

	// 子の初期化
	for (int32_t i = 0; i < parameter->GetChildrenCount(); i++)
	{
		auto pNode = (EffectNodeImplemented*) parameter->GetChild(i);

		m_generatedChildrenCount[i] = 0;
		m_nextGenerationTime[i] = pNode->CommonValues.GenerationTimeOffset.getValue(*instanceGlobal);
	}

	if( m_pParent == NULL )
	{
		// ROOTの場合

		// 状態の初期化
		m_State = INSTANCE_STATE_ACTIVE;

		// 時間周りの初期化
		m_LivingTime = 0.0f;
		m_LivedTime = FLT_MAX;

		// SRTの初期化
		m_GenerationLocation.Indentity();
		m_GlobalMatrix43.Indentity();
		m_ParentMatrix43.Indentity();

		// 親の初期化
		m_ParentMatrix43 = GetGlobalMatrix43();

		return;
	}
	
	// 親の行列を計算
	m_pParent->CalculateMatrix( 0 );

	// 状態の初期化
	m_State = INSTANCE_STATE_ACTIVE;

	// 時間周りの初期化
	m_LivingTime = 0.0f;
	m_LivedTime = (float)parameter->CommonValues.life.getValue( *instanceGlobal );


	// SRTの初期化
	m_pParent->GetGlobalMatrix43().GetTranslation( m_GlobalPosition );
	m_GlobalRevisionLocation = Vector3D(0.0f, 0.0f, 0.0f);
	m_GlobalRevisionVelocity = Vector3D(0.0f, 0.0f, 0.0f);
	m_GenerationLocation.Indentity();
	m_GlobalMatrix43.Indentity();
	m_ParentMatrix43.Indentity();

	// 親の初期化
	if( parameter->CommonValues.TranslationBindType == BindType::WhenCreating )
	{
		m_ParentMatrix43.Value[3][0] = m_pParent->m_GlobalMatrix43.Value[3][0];
		m_ParentMatrix43.Value[3][1] = m_pParent->m_GlobalMatrix43.Value[3][1];
		m_ParentMatrix43.Value[3][2] = m_pParent->m_GlobalMatrix43.Value[3][2];
	}

	if( parameter->CommonValues.RotationBindType == BindType::WhenCreating &&
		parameter->CommonValues.ScalingBindType == BindType::WhenCreating )
	{
		for( int m = 0; m < 3; m++ )
		{
			for( int n = 0; n < 3; n++ )
			{
				m_ParentMatrix43.Value[m][n] = m_pParent->m_GlobalMatrix43.Value[m][n];
			}
		}
	}
	else if ( parameter->CommonValues.RotationBindType == BindType::WhenCreating )
	{
		for( int m = 0; m < 3; m++ )
		{
			for( int n = 0; n < 3; n++ )
			{
				m_ParentMatrix43.Value[m][n] = m_pParent->m_GlobalMatrix43.Value[m][n];
			}
		}

		float s[3];
		for( int m = 0; m < 3; m++ )
		{
			s[m] = 0;
			for( int n = 0; n < 3; n++ )
			{
				s[m] += m_ParentMatrix43.Value[m][n] * m_ParentMatrix43.Value[m][n];
			}
			s[m] = sqrt( s[m] );
		}

		for( int m = 0; m < 3; m++ )
		{
			for( int n = 0; n < 3; n++ )
			{
				m_ParentMatrix43.Value[m][n] = m_ParentMatrix43.Value[m][n] / s[m];
			}
		}
	}
	else if ( parameter->CommonValues.ScalingBindType == BindType::WhenCreating )
	{
		float s[3];
		for( int m = 0; m < 3; m++ )
		{
			s[m] = 0;
			for( int n = 0; n < 3; n++ )
			{
				s[m] += m_pParent->m_GlobalMatrix43.Value[m][n] * m_pParent->m_GlobalMatrix43.Value[m][n];
			}
			s[m] = sqrt( s[m] );
		}
		m_ParentMatrix43.Value[0][0] = s[0];
		m_ParentMatrix43.Value[1][1] = s[1];
		m_ParentMatrix43.Value[2][2] = s[2];
	}
	
	// Initialize parent color
	if (parameter->RendererCommon.ColorBindType == BindType::Always)
	{
		ColorParent = m_pParent->ColorInheritance;
	}
	else if (parameter->RendererCommon.ColorBindType == BindType::WhenCreating)
	{
		ColorParent = m_pParent->ColorInheritance;
	}

	/* 位置 */
	if( m_pEffectNode->TranslationType == ParameterTranslationType_Fixed )
	{
	}
	else if( m_pEffectNode->TranslationType == ParameterTranslationType_PVA )
	{
		translation_values.random.location = m_pEffectNode->TranslationPVA.location.getValue( *this->m_pContainer->GetRootInstance() );
		translation_values.random.velocity = m_pEffectNode->TranslationPVA.velocity.getValue(*this->m_pContainer->GetRootInstance());
		translation_values.random.acceleration = m_pEffectNode->TranslationPVA.acceleration.getValue(*this->m_pContainer->GetRootInstance());
	}
	else if( m_pEffectNode->TranslationType == ParameterTranslationType_Easing )
	{
		translation_values.easing.start = m_pEffectNode->TranslationEasing.start.getValue(*this->m_pContainer->GetRootInstance());
		translation_values.easing.end = m_pEffectNode->TranslationEasing.end.getValue(*this->m_pContainer->GetRootInstance());
	}
	else if( m_pEffectNode->TranslationType == ParameterTranslationType_FCurve )
	{
		assert( m_pEffectNode->TranslationFCurve != NULL );

		translation_values.fcruve.offset.x = m_pEffectNode->TranslationFCurve->X.GetOffset( *instanceGlobal );
		translation_values.fcruve.offset.y = m_pEffectNode->TranslationFCurve->Y.GetOffset( *instanceGlobal );
		translation_values.fcruve.offset.z = m_pEffectNode->TranslationFCurve->Z.GetOffset( *instanceGlobal );
	}
	
	/* 回転 */
	if( m_pEffectNode->RotationType == ParameterRotationType_Fixed )
	{
	}
	else if( m_pEffectNode->RotationType == ParameterRotationType_PVA )
	{
		rotation_values.random.rotation = m_pEffectNode->RotationPVA.rotation.getValue(*instanceGlobal);
		rotation_values.random.velocity = m_pEffectNode->RotationPVA.velocity.getValue(*instanceGlobal);
		rotation_values.random.acceleration = m_pEffectNode->RotationPVA.acceleration.getValue(*instanceGlobal);
	}
	else if( m_pEffectNode->RotationType == ParameterRotationType_Easing )
	{
		rotation_values.easing.start = m_pEffectNode->RotationEasing.start.getValue(*instanceGlobal);
		rotation_values.easing.end = m_pEffectNode->RotationEasing.end.getValue(*instanceGlobal);
	}
	else if( m_pEffectNode->RotationType == ParameterRotationType_AxisPVA )
	{
		rotation_values.axis.random.rotation = m_pEffectNode->RotationAxisPVA.rotation.getValue(*instanceGlobal);
		rotation_values.axis.random.velocity = m_pEffectNode->RotationAxisPVA.velocity.getValue(*instanceGlobal);
		rotation_values.axis.random.acceleration = m_pEffectNode->RotationAxisPVA.acceleration.getValue(*instanceGlobal);
		rotation_values.axis.rotation = rotation_values.axis.random.rotation;
		rotation_values.axis.axis = m_pEffectNode->RotationAxisPVA.axis.getValue(*instanceGlobal);
		rotation_values.axis.axis.normalize();
	}
	else if( m_pEffectNode->RotationType == ParameterRotationType_AxisEasing )
	{
		rotation_values.axis.easing.start = m_pEffectNode->RotationAxisEasing.easing.start.getValue(*instanceGlobal);
		rotation_values.axis.easing.end = m_pEffectNode->RotationAxisEasing.easing.end.getValue(*instanceGlobal);
		rotation_values.axis.rotation = rotation_values.axis.easing.start;
		rotation_values.axis.axis = m_pEffectNode->RotationAxisEasing.axis.getValue(*instanceGlobal);
		rotation_values.axis.axis.normalize();
	}
	else if( m_pEffectNode->RotationType == ParameterRotationType_FCurve )
	{
		assert( m_pEffectNode->RotationFCurve != NULL );

		rotation_values.fcruve.offset.x = m_pEffectNode->RotationFCurve->X.GetOffset( *instanceGlobal );
		rotation_values.fcruve.offset.y = m_pEffectNode->RotationFCurve->Y.GetOffset( *instanceGlobal );
		rotation_values.fcruve.offset.z = m_pEffectNode->RotationFCurve->Z.GetOffset( *instanceGlobal );
	}

	/* 拡大縮小 */
	if( m_pEffectNode->ScalingType == ParameterScalingType_Fixed )
	{
	}
	else if( m_pEffectNode->ScalingType == ParameterScalingType_PVA )
	{
		scaling_values.random.scale = m_pEffectNode->ScalingPVA.Position.getValue(*instanceGlobal);
		scaling_values.random.velocity = m_pEffectNode->ScalingPVA.Velocity.getValue(*instanceGlobal);
		scaling_values.random.acceleration = m_pEffectNode->ScalingPVA.Acceleration.getValue(*instanceGlobal);
	}
	else if( m_pEffectNode->ScalingType == ParameterScalingType_Easing )
	{
		scaling_values.easing.start = m_pEffectNode->ScalingEasing.start.getValue(*instanceGlobal);
		scaling_values.easing.end = m_pEffectNode->ScalingEasing.end.getValue(*instanceGlobal);
	}
	else if( m_pEffectNode->ScalingType == ParameterScalingType_SinglePVA )
	{
		scaling_values.single_random.scale = m_pEffectNode->ScalingSinglePVA.Position.getValue(*instanceGlobal);
		scaling_values.single_random.velocity = m_pEffectNode->ScalingSinglePVA.Velocity.getValue(*instanceGlobal);
		scaling_values.single_random.acceleration = m_pEffectNode->ScalingSinglePVA.Acceleration.getValue(*instanceGlobal);
	}
	else if( m_pEffectNode->ScalingType == ParameterScalingType_SingleEasing )
	{
		scaling_values.single_easing.start = m_pEffectNode->ScalingSingleEasing.start.getValue(*instanceGlobal);
		scaling_values.single_easing.end = m_pEffectNode->ScalingSingleEasing.end.getValue(*instanceGlobal);
	}
	else if( m_pEffectNode->ScalingType == ParameterScalingType_FCurve )
	{
		assert( m_pEffectNode->ScalingFCurve != NULL );

		scaling_values.fcruve.offset.x = m_pEffectNode->ScalingFCurve->X.GetOffset( *instanceGlobal );
		scaling_values.fcruve.offset.y = m_pEffectNode->ScalingFCurve->Y.GetOffset( *instanceGlobal );
		scaling_values.fcruve.offset.z = m_pEffectNode->ScalingFCurve->Z.GetOffset( *instanceGlobal );
	}

	// Spawning Method
	if( m_pEffectNode->GenerationLocation.type == ParameterGenerationLocation::TYPE_POINT )
	{
		vector3d p = m_pEffectNode->GenerationLocation.point.location.getValue(*instanceGlobal);
		m_GenerationLocation.Translation( p.x, p.y, p.z );
	}
	else if (m_pEffectNode->GenerationLocation.type == ParameterGenerationLocation::TYPE_LINE)
	{
		vector3d s = m_pEffectNode->GenerationLocation.line.position_start.getValue(*instanceGlobal);
		vector3d e = m_pEffectNode->GenerationLocation.line.position_end.getValue(*instanceGlobal);
		auto noize = m_pEffectNode->GenerationLocation.line.position_noize.getValue(*instanceGlobal);
		auto division = Max(1, m_pEffectNode->GenerationLocation.line.division);

		Vector3D dir;
		(e - s).setValueToArg(dir);

		if (Vector3D::LengthSq(dir) < 0.001)
		{
			m_GenerationLocation.Translation(0 ,0, 0);
		}
		else
		{
			auto len = Vector3D::Length(dir);
			dir /= len;
		
			int32_t target = 0;
			if (m_pEffectNode->GenerationLocation.line.type == ParameterGenerationLocation::LineType::Order)
			{
				target = instanceNumber % division;
			}
			else if (m_pEffectNode->GenerationLocation.line.type == ParameterGenerationLocation::LineType::Random)
			{
				target = (int32_t)((division) * instanceGlobal->GetRand());
				if (target == division) target -= 1;
			}

			auto d = 0.0f;
			if (division > 1)
			{
				d = (len / (float)(division-1)) * target;
			}

			d += noize;
		
			s.x += dir.X * d;
			s.y += dir.Y * d;
			s.z += dir.Z * d;

			Vector3D xdir;
			Vector3D ydir;
			Vector3D zdir;

			if (fabs(dir.Y) > 0.999f)
			{
				xdir = dir;
				Vector3D::Cross(zdir, xdir, Vector3D(-1, 0, 0));
				Vector3D::Normal(zdir, zdir);
				Vector3D::Cross(ydir, zdir, xdir);
				Vector3D::Normal(ydir, ydir);
			}
			else
			{
				xdir = dir;
				Vector3D::Cross(ydir, Vector3D(0, 0, 1), xdir);
				Vector3D::Normal(ydir, ydir);
				Vector3D::Cross(zdir, xdir, ydir);
				Vector3D::Normal(zdir, zdir);
			}

			if (m_pEffectNode->GenerationLocation.EffectsRotation)
			{
				m_GenerationLocation.Value[0][0] = xdir.X;
				m_GenerationLocation.Value[0][1] = xdir.Y;
				m_GenerationLocation.Value[0][2] = xdir.Z;

				m_GenerationLocation.Value[1][0] = ydir.X;
				m_GenerationLocation.Value[1][1] = ydir.Y;
				m_GenerationLocation.Value[1][2] = ydir.Z;

				m_GenerationLocation.Value[2][0] = zdir.X;
				m_GenerationLocation.Value[2][1] = zdir.Y;
				m_GenerationLocation.Value[2][2] = zdir.Z;
			}
			else
			{
				m_GenerationLocation.Indentity();
			}

			m_GenerationLocation.Value[3][0] = s.x;
			m_GenerationLocation.Value[3][1] = s.y;
			m_GenerationLocation.Value[3][2] = s.z;
		}
	}
	else if( m_pEffectNode->GenerationLocation.type == ParameterGenerationLocation::TYPE_SPHERE )
	{
		Matrix43 mat_x, mat_y;
		mat_x.RotationX( m_pEffectNode->GenerationLocation.sphere.rotation_x.getValue( *instanceGlobal ) );
		mat_y.RotationY( m_pEffectNode->GenerationLocation.sphere.rotation_y.getValue( *instanceGlobal ) );
		float r = m_pEffectNode->GenerationLocation.sphere.radius.getValue(*instanceGlobal);
		m_GenerationLocation.Translation( 0, r, 0 );
		Matrix43::Multiple( m_GenerationLocation, m_GenerationLocation, mat_x );
		Matrix43::Multiple( m_GenerationLocation, m_GenerationLocation, mat_y );
	}
	else if( m_pEffectNode->GenerationLocation.type == ParameterGenerationLocation::TYPE_MODEL )
	{
		m_GenerationLocation.Indentity();

		int32_t modelIndex = m_pEffectNode->GenerationLocation.model.index;
		if( modelIndex >= 0 )
		{
			Model* model = (Model*)m_pEffectNode->GetEffect()->GetModel( modelIndex );
			if( model != NULL )
			{
				Model::Emitter emitter;
				
				if( m_pEffectNode->GenerationLocation.model.type == ParameterGenerationLocation::MODELTYPE_RANDOM )
				{
					emitter = model->GetEmitter( 
						instanceGlobal, 
						m_LivingTime,
						m_pManager->GetCoordinateSystem(), 
						((EffectImplemented*)m_pEffectNode->GetEffect())->GetMaginification() );
				}
				else if( m_pEffectNode->GenerationLocation.model.type == ParameterGenerationLocation::MODELTYPE_VERTEX )
				{
					emitter = model->GetEmitterFromVertex( 
						instanceNumber,
						m_LivingTime,
						m_pManager->GetCoordinateSystem(), 
						((EffectImplemented*)m_pEffectNode->GetEffect())->GetMaginification() );
				}
				else if( m_pEffectNode->GenerationLocation.model.type == ParameterGenerationLocation::MODELTYPE_VERTEX_RANDOM )
				{
					emitter = model->GetEmitterFromVertex( 
						instanceGlobal,
						m_LivingTime,
						m_pManager->GetCoordinateSystem(), 
						((EffectImplemented*)m_pEffectNode->GetEffect())->GetMaginification() );
				}
				else if( m_pEffectNode->GenerationLocation.model.type == ParameterGenerationLocation::MODELTYPE_FACE )
				{
					emitter = model->GetEmitterFromFace( 
						instanceNumber,
						m_LivingTime,
						m_pManager->GetCoordinateSystem(), 
						((EffectImplemented*)m_pEffectNode->GetEffect())->GetMaginification() );
				}
				else if( m_pEffectNode->GenerationLocation.model.type == ParameterGenerationLocation::MODELTYPE_FACE_RANDOM )
				{
					emitter = model->GetEmitterFromFace( 
						instanceGlobal,
						m_LivingTime,
						m_pManager->GetCoordinateSystem(), 
						((EffectImplemented*)m_pEffectNode->GetEffect())->GetMaginification() );
				}

				m_GenerationLocation.Translation( 
					emitter.Position.X, 
					emitter.Position.Y,
					emitter.Position.Z );

				if( m_pEffectNode->GenerationLocation.EffectsRotation )
				{
					m_GenerationLocation.Value[0][0] = emitter.Binormal.X;
					m_GenerationLocation.Value[0][1] = emitter.Binormal.Y;
					m_GenerationLocation.Value[0][2] = emitter.Binormal.Z;

					m_GenerationLocation.Value[1][0] = emitter.Tangent.X;
					m_GenerationLocation.Value[1][1] = emitter.Tangent.Y;
					m_GenerationLocation.Value[1][2] = emitter.Tangent.Z;

					m_GenerationLocation.Value[2][0] = emitter.Normal.X;
					m_GenerationLocation.Value[2][1] = emitter.Normal.Y;
					m_GenerationLocation.Value[2][2] = emitter.Normal.Z;
				}
			}
		}
	}
	else if( m_pEffectNode->GenerationLocation.type == ParameterGenerationLocation::TYPE_CIRCLE )
	{
		m_GenerationLocation.Indentity();
		float radius = m_pEffectNode->GenerationLocation.circle.radius.getValue(*instanceGlobal);
		float start = m_pEffectNode->GenerationLocation.circle.angle_start.getValue(*instanceGlobal);
		float end = m_pEffectNode->GenerationLocation.circle.angle_end.getValue(*instanceGlobal);
		int32_t div = Max(m_pEffectNode->GenerationLocation.circle.division, 1);

		int32_t target = 0;
		if(m_pEffectNode->GenerationLocation.circle.type == ParameterGenerationLocation::CIRCLE_TYPE_ORDER)
		{
			target = instanceNumber % div;
		}
		else if(m_pEffectNode->GenerationLocation.circle.type == ParameterGenerationLocation::CIRCLE_TYPE_REVERSE_ORDER)
		{
			target = div - 1 - (instanceNumber % div);
		}
		else if(m_pEffectNode->GenerationLocation.circle.type == ParameterGenerationLocation::CIRCLE_TYPE_RANDOM)
		{
			target = (int32_t)( (div) * instanceGlobal->GetRand() );
			if (target == div) target -= 1;
		}

		float angle = (end - start) * ((float)target / (float)div) + start;

		angle += m_pEffectNode->GenerationLocation.circle.angle_noize.getValue(*instanceGlobal);

		Matrix43 mat;
		if (m_pEffectNode->GenerationLocation.circle.axisDirection == ParameterGenerationLocation::AxisType::X)
		{
			mat.RotationX(angle);
			m_GenerationLocation.Translation(0, 0, radius);
		}
		if (m_pEffectNode->GenerationLocation.circle.axisDirection == ParameterGenerationLocation::AxisType::Y)
		{
			mat.RotationY(angle);
			m_GenerationLocation.Translation(radius, 0, 0);
		}
		if (m_pEffectNode->GenerationLocation.circle.axisDirection == ParameterGenerationLocation::AxisType::Z)
		{
			mat.RotationZ(angle);
			m_GenerationLocation.Translation(0, radius, 0);
		}

		
		Matrix43::Multiple( m_GenerationLocation, m_GenerationLocation, mat );
	}

	if( m_pEffectNode->SoundType == ParameterSoundType_Use )
	{
		soundValues.delay = m_pEffectNode->Sound.Delay.getValue( *instanceGlobal );
	}

	// UV
	if (m_pEffectNode->RendererCommon.UVType == ParameterRendererCommon::UV_ANIMATION)
	{
		uvTimeOffset = m_pEffectNode->RendererCommon.UV.Animation.StartFrame.getValue(*instanceGlobal);
		uvTimeOffset *= m_pEffectNode->RendererCommon.UV.Animation.FrameLength;
	}
	
	if (m_pEffectNode->RendererCommon.UVType == ParameterRendererCommon::UV_SCROLL)
	{
		auto xy = m_pEffectNode->RendererCommon.UV.Scroll.Position.getValue(*instanceGlobal);
		auto zw = m_pEffectNode->RendererCommon.UV.Scroll.Size.getValue(*instanceGlobal);

		uvAreaOffset.X = xy.x;
		uvAreaOffset.Y = xy.y;
		uvAreaOffset.Width = zw.x;
		uvAreaOffset.Height = zw.y;

		m_pEffectNode->RendererCommon.UV.Scroll.Speed.getValue(*instanceGlobal).setValueToArg(uvScrollSpeed);
	}

	if (m_pEffectNode->RendererCommon.UVType == ParameterRendererCommon::UV_FCURVE)
	{
		uvAreaOffset.X = m_pEffectNode->RendererCommon.UV.FCurve.Position->X.GetOffset(*instanceGlobal);
		uvAreaOffset.Y = m_pEffectNode->RendererCommon.UV.FCurve.Position->Y.GetOffset(*instanceGlobal);
		uvAreaOffset.Width = m_pEffectNode->RendererCommon.UV.FCurve.Size->X.GetOffset(*instanceGlobal);
		uvAreaOffset.Height = m_pEffectNode->RendererCommon.UV.FCurve.Size->Y.GetOffset(*instanceGlobal);
	}

	m_pEffectNode->InitializeRenderedInstance(*this, m_pManager);

	// Generate zero frame effect
	{
		InstanceGroup* group = m_headGroups;

		for (int32_t i = 0; i < parameter->GetChildrenCount(); i++, group = group->NextUsedByInstance)
		{
			auto node = (EffectNodeImplemented*) parameter->GetChild(i);
			auto container = m_pContainer->GetChild(i);
			assert(group != NULL);

			while (true)
			{
				// GenerationTimeOffset can be minus value.
				// Minus frame particles is generated simultaniously at frame 0.
				if (node->CommonValues.MaxGeneration > m_generatedChildrenCount[i] &&
					m_nextGenerationTime[i] <= 0.0f)
				{
					// Create a particle
					auto newInstance = group->CreateInstance();
					if (newInstance != nullptr)
					{
						newInstance->Initialize(this, m_generatedChildrenCount[i]);
					}

					m_generatedChildrenCount[i]++;
					m_nextGenerationTime[i] += Max(0.0f, node->CommonValues.GenerationTime.getValue(*instanceGlobal));
				}
				else
				{
					break;
				}
			}
		}
	}
}

//----------------------------------------------------------------------------------
//
//----------------------------------------------------------------------------------
void Instance::Update( float deltaFrame, bool shown )
{
	assert(this->m_pContainer != nullptr);
	
	// Invalidate matrix
	m_GlobalMatrix43Calculated = false;
	m_ParentMatrix43Calculated = false;

	auto instanceGlobal = this->m_pContainer->GetRootInstance();

	if (m_stepTime && m_pEffectNode->GetType() != EFFECT_NODE_TYPE_ROOT)
	{
		/* 音の更新(現状放置) */
		if (m_pEffectNode->SoundType == ParameterSoundType_Use)
		{
			float living_time = m_LivingTime;
			float living_time_p = living_time + deltaFrame;

			if (living_time <= (float) soundValues.delay && (float) soundValues.delay < living_time_p)
			{
				m_pEffectNode->PlaySound_(*this, m_pContainer->GetRootInstance(), m_pManager);
			}
		}
	}

	float originalTime = m_LivingTime;

	if(shown)
	{
		CalculateMatrix( deltaFrame );
	}
	else if( m_pEffectNode->LocationAbs.type != LocationAbsType::None )
	{
		// If attraction forces are not default, updating is needed in each frame.
		CalculateMatrix( deltaFrame );
	}

	// Get parent color.
	if (m_pParent != NULL)
	{
		if (m_pEffectNode->RendererCommon.ColorBindType == BindType::Always)
		{
			ColorParent = m_pParent->ColorInheritance;
		}
	}

	/* 親の削除処理 */
	if (m_pParent != NULL && m_pParent->GetState() != INSTANCE_STATE_ACTIVE)
	{
		CalculateParentMatrix( deltaFrame );
		m_pParent = nullptr;
	}

	/* 時間の進行 */
	if(  m_stepTime )
	{
		m_LivingTime += deltaFrame;
	}

	// 子の処理
	if( m_stepTime && (originalTime <= m_LivedTime || !m_pEffectNode->CommonValues.RemoveWhenLifeIsExtinct) )
	{
		InstanceGroup* group = m_headGroups;

		for (int i = 0; i < m_pEffectNode->GetChildrenCount(); i++, group = group->NextUsedByInstance)
		{
			auto pNode = (EffectNodeImplemented*) m_pEffectNode->GetChild(i);
			auto pContainer = m_pContainer->GetChild( i );
			assert( group != NULL );

			// Create a particle
			while (true)
			{
				if (pNode->CommonValues.MaxGeneration > m_generatedChildrenCount[i] &&
					originalTime + deltaFrame >= m_nextGenerationTime[i])
				{
					// 生成処理
					Instance* pNewInstance = group->CreateInstance();
					if (pNewInstance != NULL)
					{
						pNewInstance->Initialize(this, m_generatedChildrenCount[i]);
					}
					else
					{
						break;
					}

					m_generatedChildrenCount[i]++;
					m_nextGenerationTime[i] += Max(0.0f, pNode->CommonValues.GenerationTime.getValue(*instanceGlobal));
				}
				else
				{
					break;
				}
			}
		}
	}
	
	// 死亡判定
	bool killed = false;
	if( m_pEffectNode->GetType() != EFFECT_NODE_TYPE_ROOT )
	{
		// 時間経過
		if( m_pEffectNode->CommonValues.RemoveWhenLifeIsExtinct )
		{
			if( m_LivingTime > m_LivedTime )
			{
				killed = true;
			}
		}

		// 親が消えた場合
		if( m_pEffectNode->CommonValues.RemoveWhenParentIsRemoved )
		{
			if( m_pParent == nullptr || m_pParent->GetState() != INSTANCE_STATE_ACTIVE )
			{
				m_pParent = nullptr;
				killed = true;
			}
		}

		// 子が全て消えた場合
		if( !killed && m_pEffectNode->CommonValues.RemoveWhenChildrenIsExtinct )
		{
			int maxcreate_count = 0;
			InstanceGroup* group = m_headGroups;

			for (int i = 0; i < m_pEffectNode->GetChildrenCount(); i++, group = group->NextUsedByInstance)
			{
				auto child = (EffectNodeImplemented*) m_pEffectNode->GetChild(i);

				float last_generation_time = 
					child->CommonValues.GenerationTime.max *
					(child->CommonValues.MaxGeneration - 1) +
					child->CommonValues.GenerationTimeOffset.max +
					1.0f;

				if( m_LivingTime >= last_generation_time && group->GetInstanceCount() == 0 )
				{
					maxcreate_count++;
				}
				else
				{
					break;
				}
			}
			
			if( maxcreate_count == m_pEffectNode->GetChildrenCount() )
			{
				killed = true;
			}
		}
	}

	if(killed)
	{
		/* 死亡確定時、計算が必要な場合は計算をする。*/
		if( m_pEffectNode->GetChildrenCount() > 0)
		{
			// Get parent color.
			if (m_pParent != NULL)
			{
				if (m_pEffectNode->RendererCommon.ColorBindType == BindType::Always)
				{
					ColorParent = m_pParent->ColorInheritance;
				}
			}
		}

		// Delete this particle with myself.
		Kill();
		return;
	}

	// 時間の進行許可
	m_stepTime = true;
}

//----------------------------------------------------------------------------------
//
//----------------------------------------------------------------------------------
void Instance::CalculateMatrix( float deltaFrame )
{
	// 計算済なら終了
	if( m_GlobalMatrix43Calculated ) return;
	
	//if( m_sequenceNumber == ((ManagerImplemented*)m_pManager)->GetSequenceNumber() ) return;
	m_sequenceNumber = ((ManagerImplemented*)m_pManager)->GetSequenceNumber();

	assert( m_pEffectNode != NULL );
	assert( m_pContainer != NULL );

	// 親の処理
	if( m_pParent != NULL )
	{
		CalculateParentMatrix( deltaFrame );
	}

	Vector3D localPosition;
	Vector3D localAngle;
	Vector3D localScaling;

	/* 更新処理 */
	if( m_pEffectNode->GetType() != EFFECT_NODE_TYPE_ROOT )
	{
		/* 位置の更新(時間から直接求めれるよう対応済み) */
		if( m_pEffectNode->TranslationType == ParameterTranslationType_None )
		{
			localPosition.X = 0;
			localPosition.Y = 0;
			localPosition.Z = 0;
		}
		else if( m_pEffectNode->TranslationType == ParameterTranslationType_Fixed )
		{
			localPosition.X = m_pEffectNode->TranslationFixed.Position.X;
			localPosition.Y = m_pEffectNode->TranslationFixed.Position.Y;
			localPosition.Z = m_pEffectNode->TranslationFixed.Position.Z;
		}
		else if( m_pEffectNode->TranslationType == ParameterTranslationType_PVA )
		{
			/* 現在位置 = 初期座標 + (初期速度 * t) + (初期加速度 * t * t * 0.5)*/
			localPosition.X = translation_values.random.location.x +
				(translation_values.random.velocity.x * m_LivingTime) +
				(translation_values.random.acceleration.x * m_LivingTime * m_LivingTime * 0.5f);

			localPosition.Y = translation_values.random.location.y +
				(translation_values.random.velocity.y * m_LivingTime) +
				(translation_values.random.acceleration.y * m_LivingTime * m_LivingTime * 0.5f);

			localPosition.Z = translation_values.random.location.z +
				(translation_values.random.velocity.z * m_LivingTime) +
				(translation_values.random.acceleration.z * m_LivingTime * m_LivingTime * 0.5f);

		}
		else if( m_pEffectNode->TranslationType == ParameterTranslationType_Easing )
		{
			m_pEffectNode->TranslationEasing.setValueToArg(
				localPosition,
				translation_values.easing.start,
				translation_values.easing.end,
				m_LivingTime / m_LivedTime );
		}
		else if( m_pEffectNode->TranslationType == ParameterTranslationType_FCurve )
		{
			assert( m_pEffectNode->TranslationFCurve != NULL );
			localPosition.X = m_pEffectNode->TranslationFCurve->X.GetValue( (int)m_LivingTime ) + translation_values.fcruve.offset.x;
			localPosition.Y = m_pEffectNode->TranslationFCurve->Y.GetValue( (int)m_LivingTime ) + translation_values.fcruve.offset.y;
			localPosition.Z = m_pEffectNode->TranslationFCurve->Z.GetValue( (int)m_LivingTime ) + translation_values.fcruve.offset.z;
		}

		if( !m_pEffectNode->GenerationLocation.EffectsRotation )
		{
			localPosition.X += m_GenerationLocation.Value[3][0];
			localPosition.Y += m_GenerationLocation.Value[3][1];
			localPosition.Z += m_GenerationLocation.Value[3][2];
		}

		/* 回転の更新(時間から直接求めれるよう対応済み) */
		if( m_pEffectNode->RotationType == ParameterRotationType_None )
		{
			localAngle.X = 0;
			localAngle.Y = 0;
			localAngle.Z = 0;
		}
		else if( m_pEffectNode->RotationType == ParameterRotationType_Fixed )
		{
			localAngle.X = m_pEffectNode->RotationFixed.Position.X;
			localAngle.Y = m_pEffectNode->RotationFixed.Position.Y;
			localAngle.Z = m_pEffectNode->RotationFixed.Position.Z;
		}
		else if( m_pEffectNode->RotationType == ParameterRotationType_PVA )
		{
			/* 現在位置 = 初期座標 + (初期速度 * t) + (初期加速度 * t * t * 0.5)*/
			localAngle.X = rotation_values.random.rotation.x +
				(rotation_values.random.velocity.x * m_LivingTime) +
				(rotation_values.random.acceleration.x * m_LivingTime * m_LivingTime * 0.5f);

			localAngle.Y = rotation_values.random.rotation.y +
				(rotation_values.random.velocity.y * m_LivingTime) +
				(rotation_values.random.acceleration.y * m_LivingTime * m_LivingTime * 0.5f);

			localAngle.Z = rotation_values.random.rotation.z +
				(rotation_values.random.velocity.z * m_LivingTime) +
				(rotation_values.random.acceleration.z * m_LivingTime * m_LivingTime * 0.5f);

		}
		else if( m_pEffectNode->RotationType == ParameterRotationType_Easing )
		{
			m_pEffectNode->RotationEasing.setValueToArg(
				localAngle,
				rotation_values.easing.start,
				rotation_values.easing.end,
				m_LivingTime / m_LivedTime );
		}
		else if( m_pEffectNode->RotationType == ParameterRotationType_AxisPVA )
		{
			rotation_values.axis.rotation = 
				rotation_values.axis.random.rotation +
				rotation_values.axis.random.velocity * m_LivingTime +
				rotation_values.axis.random.acceleration * m_LivingTime * m_LivingTime * 0.5f;
		}
		else if( m_pEffectNode->RotationType == ParameterRotationType_AxisEasing )
		{
			m_pEffectNode->RotationAxisEasing.easing.setValueToArg(
				rotation_values.axis.rotation,
				rotation_values.axis.easing.start,
				rotation_values.axis.easing.end,
				m_LivingTime / m_LivedTime );
		}
		else if( m_pEffectNode->RotationType == ParameterRotationType_FCurve )
		{
			assert( m_pEffectNode->RotationFCurve != NULL );
			localAngle.X = m_pEffectNode->RotationFCurve->X.GetValue( (int)m_LivingTime ) + rotation_values.fcruve.offset.x;
			localAngle.Y = m_pEffectNode->RotationFCurve->Y.GetValue( (int)m_LivingTime ) + rotation_values.fcruve.offset.y;
			localAngle.Z = m_pEffectNode->RotationFCurve->Z.GetValue( (int)m_LivingTime ) + rotation_values.fcruve.offset.z;
		}

		/* 拡大の更新(時間から直接求めれるよう対応済み) */
		if( m_pEffectNode->ScalingType == ParameterScalingType_None )
		{
			localScaling.X = 1.0f;
			localScaling.Y = 1.0f;
			localScaling.Z = 1.0f;
		}
		else if( m_pEffectNode->ScalingType == ParameterScalingType_Fixed )
		{
			localScaling.X = m_pEffectNode->ScalingFixed.Position.X;
			localScaling.Y = m_pEffectNode->ScalingFixed.Position.Y;
			localScaling.Z = m_pEffectNode->ScalingFixed.Position.Z;
		}
		else if( m_pEffectNode->ScalingType == ParameterScalingType_PVA )
		{
			/* 現在位置 = 初期座標 + (初期速度 * t) + (初期加速度 * t * t * 0.5)*/
			localScaling.X = scaling_values.random.scale.x +
				(scaling_values.random.velocity.x * m_LivingTime) +
				(scaling_values.random.acceleration.x * m_LivingTime * m_LivingTime * 0.5f);

			localScaling.Y = scaling_values.random.scale.y +
				(scaling_values.random.velocity.y * m_LivingTime) +
				(scaling_values.random.acceleration.y * m_LivingTime * m_LivingTime * 0.5f);

			localScaling.Z = scaling_values.random.scale.z +
				(scaling_values.random.velocity.z * m_LivingTime) +
				(scaling_values.random.acceleration.z * m_LivingTime * m_LivingTime * 0.5f);
		}
		else if( m_pEffectNode->ScalingType == ParameterScalingType_Easing )
		{
			m_pEffectNode->ScalingEasing.setValueToArg(
				localScaling,
				scaling_values.easing.start,
				scaling_values.easing.end,
				m_LivingTime / m_LivedTime );
		}
		else if( m_pEffectNode->ScalingType == ParameterScalingType_SinglePVA )
		{
			float s = scaling_values.single_random.scale +
				scaling_values.single_random.velocity * m_LivingTime +
				scaling_values.single_random.acceleration * m_LivingTime * m_LivingTime * 0.5f;
			localScaling.X = s;
			localScaling.Y = s;
			localScaling.Z = s;
		}
		else if( m_pEffectNode->ScalingType == ParameterScalingType_SingleEasing )
		{
			float scale;
			m_pEffectNode->ScalingSingleEasing.setValueToArg(
				scale,
				scaling_values.single_easing.start,
				scaling_values.single_easing.end,
				m_LivingTime / m_LivedTime );
			localScaling.X = scale;
			localScaling.Y = scale;
			localScaling.Z = scale;
		}
		else if( m_pEffectNode->ScalingType == ParameterScalingType_FCurve )
		{
			assert( m_pEffectNode->ScalingFCurve != NULL );

			localScaling.X = m_pEffectNode->ScalingFCurve->X.GetValue( (int32_t)m_LivingTime ) + scaling_values.fcruve.offset.x;
			localScaling.Y = m_pEffectNode->ScalingFCurve->Y.GetValue( (int32_t)m_LivingTime ) + scaling_values.fcruve.offset.y;
			localScaling.Z = m_pEffectNode->ScalingFCurve->Z.GetValue( (int32_t)m_LivingTime ) + scaling_values.fcruve.offset.z;
		}

		/* 描画部分の更新 */
		m_pEffectNode->UpdateRenderedInstance( *this, m_pManager );
	}
	
	// 行列の更新
	if( m_pEffectNode->GetType() != EFFECT_NODE_TYPE_ROOT )
	{
		m_GlobalMatrix43.Scaling( localScaling.X, localScaling.Y,  localScaling.Z );

		if( m_pEffectNode->RotationType == ParameterRotationType_Fixed ||
			m_pEffectNode->RotationType == ParameterRotationType_PVA ||
			m_pEffectNode->RotationType == ParameterRotationType_Easing ||
			m_pEffectNode->RotationType == ParameterRotationType_FCurve )
		{
			Matrix43 MatRot;
			MatRot.RotationZXY( localAngle.Z, localAngle.X, localAngle.Y );
			Matrix43::Multiple( m_GlobalMatrix43, m_GlobalMatrix43, MatRot );
		}
		else if( m_pEffectNode->RotationType == ParameterRotationType_AxisPVA ||
				 m_pEffectNode->RotationType == ParameterRotationType_AxisEasing )
		{
			Matrix43 MatRot;
			Vector3D axis;
			axis.X = rotation_values.axis.axis.x;
			axis.Y = rotation_values.axis.axis.y;
			axis.Z = rotation_values.axis.axis.z;

			MatRot.RotationAxis( axis, rotation_values.axis.rotation );
			Matrix43::Multiple( m_GlobalMatrix43, m_GlobalMatrix43, MatRot );
		}

		if( localPosition.X != 0.0f ||
			localPosition.Y != 0.0f || 
			localPosition.Z != 0.0f )
		{
			Matrix43 MatTra;
			MatTra.Translation( localPosition.X, localPosition.Y, localPosition.Z );
			Matrix43::Multiple( m_GlobalMatrix43, m_GlobalMatrix43, MatTra );
		}

		if( m_pEffectNode->GenerationLocation.EffectsRotation )
		{
			Matrix43::Multiple( m_GlobalMatrix43, m_GlobalMatrix43, m_GenerationLocation );
		}

		Matrix43::Multiple( m_GlobalMatrix43, m_GlobalMatrix43, m_ParentMatrix43 );
		
		Vector3D currentPosition;
		m_GlobalMatrix43.GetTranslation( currentPosition );
		m_GlobalVelocity = currentPosition - m_GlobalPosition;
		m_GlobalPosition = currentPosition;

		if( m_pEffectNode->LocationAbs.type != LocationAbsType::None )
		{
			ModifyMatrixFromLocationAbs( deltaFrame );
		}
	}

	m_GlobalMatrix43Calculated = true;
}

void Instance::CalculateParentMatrix( float deltaFrame )
{
	// 計算済なら終了
	if( m_ParentMatrix43Calculated ) return;

	// 親の行列を計算
	m_pParent->CalculateMatrix( deltaFrame );

	if( m_pEffectNode->GetType() != EFFECT_NODE_TYPE_ROOT )
	{
		BindType lType = m_pEffectNode->CommonValues.TranslationBindType;
		BindType rType = m_pEffectNode->CommonValues.RotationBindType;
		BindType sType = m_pEffectNode->CommonValues.ScalingBindType;

		const Instance* rootInstance;
		InstanceGlobal* instanceGlobal = m_pContainer->GetRootInstance();
		InstanceContainer* rootContainer = instanceGlobal->GetRootContainer();
		InstanceGroup* firstGloup = rootContainer->GetFirstGroup();
		if( firstGloup && firstGloup->GetInstanceCount() > 0 )
		{
			rootInstance = firstGloup->GetFirst();
		}
		else
		{
			rootInstance = NULL;
		}
		
		if( (lType == BindType::Always && rType == BindType::Always && sType == BindType::Always) )
		{
			m_ParentMatrix43 = m_pParent->GetGlobalMatrix43();
		}
		else if ( lType == BindType::NotBind_Root && rType == BindType::NotBind_Root && sType == BindType::NotBind_Root )
		{
			m_ParentMatrix43 = rootInstance->GetGlobalMatrix43();
		}
		else
		{
			Vector3D s, t;
			Matrix43 r;

			m_ParentMatrix43.GetSRT( s, r, t );
			
			if( lType == BindType::Always )
			{
				m_pParent->GetGlobalMatrix43().GetTranslation( t );
			}
			else if( lType == BindType::NotBind_Root && rootInstance != NULL )
			{
				rootInstance->GetGlobalMatrix43().GetTranslation( t );
			}
			
			if( rType == BindType::Always )
			{
				m_pParent->GetGlobalMatrix43().GetRotation( r );
			}
			else if( rType == BindType::NotBind_Root && rootInstance != NULL )
			{
				rootInstance->GetGlobalMatrix43().GetRotation( r );
			}
			

			if( sType == BindType::Always )
			{
				m_pParent->GetGlobalMatrix43().GetScale( s );
			}
			else if( sType == BindType::NotBind_Root && rootInstance != NULL )
			{
				rootInstance->GetGlobalMatrix43().GetScale( s );
			}

			m_ParentMatrix43.SetSRT( s, r, t );
		}
	}
	else
	{
		// Rootの場合
		m_ParentMatrix43 = m_pParent->GetGlobalMatrix43();
	}

	m_ParentMatrix43Calculated = true;
}

//----------------------------------------------------------------------------------
//
//----------------------------------------------------------------------------------
void Instance::ModifyMatrixFromLocationAbs( float deltaFrame )
{
	InstanceGlobal* instanceGlobal = m_pContainer->GetRootInstance();

	// Update attraction forces
	if( m_pEffectNode->LocationAbs.type == LocationAbsType::None )
	{	
	}
	else if( m_pEffectNode->LocationAbs.type == LocationAbsType::Gravity )
	{
		m_GlobalRevisionLocation.X = m_pEffectNode->LocationAbs.gravity.x *
			m_LivingTime * m_LivingTime * 0.5f;
		m_GlobalRevisionLocation.Y = m_pEffectNode->LocationAbs.gravity.y *
			m_LivingTime * m_LivingTime * 0.5f;
		m_GlobalRevisionLocation.Z = m_pEffectNode->LocationAbs.gravity.z *
			m_LivingTime * m_LivingTime * 0.5f;
	}
	else if( m_pEffectNode->LocationAbs.type == LocationAbsType::AttractiveForce )
	{
		InstanceGlobal* instanceGlobal = m_pContainer->GetRootInstance();

		float force = m_pEffectNode->LocationAbs.attractiveForce.force;
		float control = m_pEffectNode->LocationAbs.attractiveForce.control;
		float minRange = m_pEffectNode->LocationAbs.attractiveForce.minRange;
		float maxRange = m_pEffectNode->LocationAbs.attractiveForce.maxRange;
		
		Vector3D position = m_GlobalPosition - m_GlobalVelocity + m_GlobalRevisionLocation;

		Vector3D targetDifference = instanceGlobal->GetTargetLocation() - position;
		float targetDistance = Vector3D::Length( targetDifference );
		if( targetDistance > 0.0f )
		{
			Vector3D targetDirection = targetDifference / targetDistance;
		
			if( minRange > 0.0f || maxRange > 0.0f )
			{
				if( targetDistance >= m_pEffectNode->LocationAbs.attractiveForce.maxRange )
				{
					force = 0.0f;
				}
				else if( targetDistance > m_pEffectNode->LocationAbs.attractiveForce.minRange )
				{
					force *= 1.0f - (targetDistance - minRange) / (maxRange - minRange);
				}
			}

			if (deltaFrame > 0)
			{
				m_GlobalRevisionVelocity += targetDirection * force * deltaFrame;
				float currentVelocity = Vector3D::Length(m_GlobalRevisionVelocity);
				Vector3D currentDirection = m_GlobalRevisionVelocity / currentVelocity;

				m_GlobalRevisionVelocity = (targetDirection * control + currentDirection * (1.0f - control)) * currentVelocity;
				m_GlobalRevisionLocation += m_GlobalRevisionVelocity * deltaFrame;
			}
		}
	}

	Matrix43 MatTraGlobal;
	MatTraGlobal.Translation( m_GlobalRevisionLocation.X, m_GlobalRevisionLocation.Y, m_GlobalRevisionLocation.Z );
	Matrix43::Multiple( m_GlobalMatrix43, m_GlobalMatrix43, MatTraGlobal );
}

//----------------------------------------------------------------------------------
//
//----------------------------------------------------------------------------------
void Instance::Draw(Instance* next)
{
	assert( m_pEffectNode != NULL );

	if( !m_pEffectNode->IsRendered ) return;

	if( m_sequenceNumber != ((ManagerImplemented*)m_pManager)->GetSequenceNumber() )
	{
		CalculateMatrix( 0 );
	}

	m_pEffectNode->Rendering(*this, next, m_pManager);
}

//----------------------------------------------------------------------------------
//
//----------------------------------------------------------------------------------
void Instance::Kill()
{
	if( m_State == INSTANCE_STATE_ACTIVE )
	{
		for( InstanceGroup* group = m_headGroups; group != NULL; group = group->NextUsedByInstance )
		{
			group->IsReferencedFromInstance = false;
		}

		m_State = INSTANCE_STATE_REMOVING;
	}
}

//----------------------------------------------------------------------------------
//
//----------------------------------------------------------------------------------
RectF Instance::GetUV() const
{
	if( m_pEffectNode->RendererCommon.UVType == ParameterRendererCommon::UV_DEFAULT )
	{
		return RectF( 0.0f, 0.0f, 1.0f, 1.0f );
	}
	else if( m_pEffectNode->RendererCommon.UVType == ParameterRendererCommon::UV_FIXED )
	{
		return RectF(
			m_pEffectNode->RendererCommon.UV.Fixed.Position.x,
			m_pEffectNode->RendererCommon.UV.Fixed.Position.y,
			m_pEffectNode->RendererCommon.UV.Fixed.Position.w,
			m_pEffectNode->RendererCommon.UV.Fixed.Position.h );
	}
	else if( m_pEffectNode->RendererCommon.UVType == ParameterRendererCommon::UV_ANIMATION )
	{
		auto time = m_LivingTime + uvTimeOffset;

		int32_t frameNum = (int32_t)(time / m_pEffectNode->RendererCommon.UV.Animation.FrameLength);
		int32_t frameCount = m_pEffectNode->RendererCommon.UV.Animation.FrameCountX * m_pEffectNode->RendererCommon.UV.Animation.FrameCountY;

		if( m_pEffectNode->RendererCommon.UV.Animation.LoopType == m_pEffectNode->RendererCommon.UV.Animation.LOOPTYPE_ONCE )
		{
			if( frameNum >= frameCount )
			{
				frameNum = frameCount - 1;
			}
		}
		else if ( m_pEffectNode->RendererCommon.UV.Animation.LoopType == m_pEffectNode->RendererCommon.UV.Animation.LOOPTYPE_LOOP )
		{
			frameNum %= frameCount;
		}
		else if ( m_pEffectNode->RendererCommon.UV.Animation.LoopType == m_pEffectNode->RendererCommon.UV.Animation.LOOPTYPE_REVERSELOOP )
		{
			bool rev = (frameNum / frameCount) % 2 == 1;
			frameNum %= frameCount;
			if( rev )
			{
				frameNum = frameCount - 1 - frameNum;
			}
		}

		int32_t frameX = frameNum % m_pEffectNode->RendererCommon.UV.Animation.FrameCountX;
		int32_t frameY = frameNum / m_pEffectNode->RendererCommon.UV.Animation.FrameCountX;

		return RectF(
			m_pEffectNode->RendererCommon.UV.Animation.Position.x + m_pEffectNode->RendererCommon.UV.Animation.Position.w * frameX,
			m_pEffectNode->RendererCommon.UV.Animation.Position.y + m_pEffectNode->RendererCommon.UV.Animation.Position.h * frameY,
			m_pEffectNode->RendererCommon.UV.Animation.Position.w,
			m_pEffectNode->RendererCommon.UV.Animation.Position.h );
	}
	else if( m_pEffectNode->RendererCommon.UVType == ParameterRendererCommon::UV_SCROLL )
	{
		auto time = m_LivingTime + uvTimeOffset;

		return RectF(
			uvAreaOffset.X + uvScrollSpeed.X * time,
			uvAreaOffset.Y + uvScrollSpeed.Y * time,
			uvAreaOffset.Width,
			uvAreaOffset.Height);
	}
	else if (m_pEffectNode->RendererCommon.UVType == ParameterRendererCommon::UV_FCURVE)
	{
		auto time = m_LivingTime + uvTimeOffset;

		return RectF(
			uvAreaOffset.X + m_pEffectNode->RendererCommon.UV.FCurve.Position->X.GetValue(time),
			uvAreaOffset.Y + m_pEffectNode->RendererCommon.UV.FCurve.Position->Y.GetValue(time),
			uvAreaOffset.Width + m_pEffectNode->RendererCommon.UV.FCurve.Size->X.GetValue(time),
			uvAreaOffset.Height + m_pEffectNode->RendererCommon.UV.FCurve.Size->Y.GetValue(time));
	}


	return RectF( 0.0f, 0.0f, 1.0f, 1.0f );
}

//----------------------------------------------------------------------------------
//
//----------------------------------------------------------------------------------
}

//----------------------------------------------------------------------------------
//
//----------------------------------------------------------------------------------