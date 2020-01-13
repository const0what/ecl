/****************************************************************************
 *
 *   Copyright (c) 2019 ECL Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * Test the external vision functionality
 * @author Kamil Ritz <ka.ritz@hotmail.com>
 */

#include <gtest/gtest.h>
#include "EKF/ekf.h"
#include "sensor_simulator/sensor_simulator.h"
#include "sensor_simulator/ekf_wrapper.h"


class EkfExternalVisionTest : public ::testing::Test {
 public:

	EkfExternalVisionTest(): ::testing::Test(),
	_ekf{std::make_shared<Ekf>()},
	_sensor_simulator(_ekf),
	_ekf_wrapper(_ekf) {};

	std::shared_ptr<Ekf> _ekf;
	SensorSimulator _sensor_simulator;
	EkfWrapper _ekf_wrapper;

	// Setup the Ekf with synthetic measurements
	void SetUp() override
	{
		_ekf->init(0);
	}

	// Use this method to clean up any memory, network etc. after each test
	void TearDown() override
	{
	}
};

TEST_F(EkfExternalVisionTest, checkVisionFusionLogic)
{
	_sensor_simulator.runSeconds(2);

	_ekf_wrapper.enableExternalVisionPositionFusion();
	_sensor_simulator.startExternalVision();
	_sensor_simulator.runSeconds(2);

	EXPECT_TRUE(_ekf_wrapper.isIntendingExternalVisionPositionFusion());
	EXPECT_FALSE(_ekf_wrapper.isIntendingExternalVisionVelocityFusion());
	EXPECT_FALSE(_ekf_wrapper.isIntendingExternalVisionHeadingFusion());

	EXPECT_TRUE(_ekf->local_position_is_valid());
	EXPECT_FALSE(_ekf->global_position_is_valid());

	_ekf_wrapper.enableExternalVisionVelocityFusion();
	_sensor_simulator.runSeconds(2);

	EXPECT_TRUE(_ekf_wrapper.isIntendingExternalVisionPositionFusion());
	EXPECT_TRUE(_ekf_wrapper.isIntendingExternalVisionVelocityFusion());
	EXPECT_FALSE(_ekf_wrapper.isIntendingExternalVisionHeadingFusion());

	EXPECT_TRUE(_ekf->local_position_is_valid());
	EXPECT_FALSE(_ekf->global_position_is_valid());

	_ekf_wrapper.enableExternalVisionHeadingFusion();
	_sensor_simulator.runSeconds(2);

	EXPECT_TRUE(_ekf_wrapper.isIntendingExternalVisionPositionFusion());
	EXPECT_TRUE(_ekf_wrapper.isIntendingExternalVisionVelocityFusion());
	EXPECT_TRUE(_ekf_wrapper.isIntendingExternalVisionHeadingFusion());

	EXPECT_TRUE(_ekf->local_position_is_valid());
	EXPECT_FALSE(_ekf->global_position_is_valid());
}

TEST_F(EkfExternalVisionTest, visionVarianceCheck)
{
	_sensor_simulator.runSeconds(2);

	const Vector3f velVar_init = _ekf_wrapper.getVelocityVariance();
	EXPECT_NEAR(velVar_init(0), velVar_init(1), 0.0001);

	float vel_cov_data [9] = {2.0f, 0.0f, 0.0f,
				  0.0f, 0.01f, 0.0f,
				  0.0f, 0.0f, 0.01f};
	Matrix3f vel_cov(vel_cov_data);
	_sensor_simulator._vio.setVelocityCovariance(vel_cov);
	_ekf_wrapper.enableExternalVisionVelocityFusion();
	_sensor_simulator.startExternalVision();
	_sensor_simulator.runSeconds(4);

	const Vector3f velVar_new = _ekf_wrapper.getVelocityVariance();
	EXPECT_TRUE(velVar_new(0) > velVar_new(1));
}

TEST_F(EkfExternalVisionTest, visionAlignment)
{
	_sensor_simulator.runSeconds(2);

	// GIVEN: Drone is pointing north, and we use mag (ROTATE_EV)
	//        Heading of drone in EKF frame is 0°

	// WHEN: Vision frame is rotate +90°. The reported heading is -90°
	Quatf externalVisionFrameOffset(Eulerf(0.0f,0.0f,math::radians(90.0f)));
	_sensor_simulator._vio.setOrientation(externalVisionFrameOffset.inversed());
	_ekf_wrapper.enableExternalVisionAlignment();

	// Simulate high uncertainty on vision x axis which is in this case
	// the y EKF frame axis
	float vel_cov_data [9] = {2.0f, 0.0f, 0.0f,
				  0.0f, 0.01f, 0.0f,
				  0.0f, 0.0f, 0.01f};
	Matrix3f vel_cov(vel_cov_data);
	_sensor_simulator._vio.setVelocityCovariance(vel_cov);
	_ekf_wrapper.enableExternalVisionVelocityFusion();
	_sensor_simulator.startExternalVision();

	const Vector3f velVar_init = _ekf_wrapper.getVelocityVariance();
	EXPECT_NEAR(velVar_init(0), velVar_init(1), 0.0001);

	_sensor_simulator.runSeconds(4);

	// THEN: velocity uncertainty in y should be bigger
	const Vector3f velVar_new = _ekf_wrapper.getVelocityVariance();
	EXPECT_TRUE(velVar_new(1) > velVar_new(0));

	// THEN: the frame offset should be estimated correctly
	Quatf estimatedExternalVisionFrameOffset = _ekf_wrapper.getVisionAlignmentQuaternion();
	EXPECT_TRUE(matrix::isEqual(externalVisionFrameOffset.canonical(),
				    estimatedExternalVisionFrameOffset.canonical()));
}

TEST_F(EkfExternalVisionTest, velocityFrameBody)
{
	const Quatf quat_sim(Eulerf(0.0f, 0.0f, math::radians(88.0f)));
	_sensor_simulator.simulateOrientation(quat_sim);
	_sensor_simulator.runSeconds(2);

	const Vector3f velVar_init = _ekf_wrapper.getVelocityVariance();
	EXPECT_NEAR(velVar_init(0), velVar_init(1), 0.0001);

	_sensor_simulator._vio.setVelocityFrameToBody();

	float vel_cov_data [9] = {2.0f, 0.0f, 0.0f,
				  0.0f, 0.01f, 0.0f,
				  0.0f, 0.0f, 0.01f};
	Matrix3f vel_cov_body(vel_cov_data);
	Vector3f vel_body(1.0f, 0.0f, 0.0f);
	_sensor_simulator._vio.setVelocityCovariance(vel_cov_body);
	_sensor_simulator._vio.setVelocity(vel_body);
	_ekf_wrapper.enableExternalVisionVelocityFusion();
	_sensor_simulator.startExternalVision();
	_sensor_simulator.runSeconds(4);

	const Vector3f velVar_new = _ekf_wrapper.getVelocityVariance();
	EXPECT_TRUE(velVar_new(1) > velVar_new(0));

	const Vector3f vel_earth_est = _ekf_wrapper.getVelocity();
	EXPECT_NEAR(vel_earth_est(0), 0.0f, 0.1f);
	EXPECT_NEAR(vel_earth_est(1), 1.0f, 0.1f);
}

TEST_F(EkfExternalVisionTest, velocityFrameLocal)
{
	const Quatf quat_sim(Eulerf(0.0f, 0.0f, math::radians(88.0f)));
	_sensor_simulator.simulateOrientation(quat_sim);
	_sensor_simulator.runSeconds(2);

	const Vector3f velVar_init = _ekf_wrapper.getVelocityVariance();
	EXPECT_NEAR(velVar_init(0), velVar_init(1), 0.0001);

	_sensor_simulator._vio.setVelocityFrameToLocal();

	float vel_cov_data [9] = {2.0f, 0.0f, 0.0f,
				  0.0f, 0.01f, 0.0f,
				  0.0f, 0.0f, 0.01f};
	Matrix3f vel_cov_earth(vel_cov_data);
	Vector3f vel_earth(1.0f, 0.0f, 0.0f);
	_sensor_simulator._vio.setVelocityCovariance(vel_cov_earth);
	_sensor_simulator._vio.setVelocity(vel_earth);
	_ekf_wrapper.enableExternalVisionVelocityFusion();
	_sensor_simulator.startExternalVision();
	_sensor_simulator.runSeconds(4);

	const Vector3f velVar_new = _ekf_wrapper.getVelocityVariance();
	EXPECT_TRUE(velVar_new(0) > velVar_new(1));

	const Vector3f vel_earth_est = _ekf_wrapper.getVelocity();
	EXPECT_NEAR(vel_earth_est(0), 1.0f, 0.1f);
	EXPECT_NEAR(vel_earth_est(1), 0.0f, 0.1f);
}
