// ==========================================================================
// 
// creepMiner - Burstcoin cryptocurrency CPU and GPU miner
// Copyright (C)  2016-2018 Creepsky (creepsky@gmail.com)
// 
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110 - 1301  USA
// 
// ==========================================================================

#include "PlotGenerator.hpp"
#include "Declarations.hpp"
#include "shabal/MinerShabal.hpp"
#include "mining/Miner.hpp"
#include "PlotVerifier.hpp"
#include "MinerUtil.hpp"
#include <fstream>
#include <random>

std::vector<char> Burst::PlotGenerator::generate(Poco::UInt64 account, Poco::UInt64 nonce)
{
	char final[32];
	std::vector<char> gendata;
	gendata.resize(Settings::PlotSize + 16);

	auto xv = reinterpret_cast<char*>(&account);

	for (auto j = 0u; j <= 7; ++j)
		gendata[Settings::PlotSize + j] = xv[7 - j];

	xv = reinterpret_cast<char*>(&nonce);

	for (auto j = 0u; j <= 7; ++j)
		gendata[Settings::PlotSize + 8 + j] = xv[7 - j];

	for (auto i = Settings::PlotSize; i > 0; i -= Settings::HashSize)
	{
		Shabal256_SSE2 x;

		auto len = Settings::PlotSize + 16 - i;

		if (len > Settings::ScoopPerPlot)
			len = Settings::ScoopPerPlot;

		x.update(&gendata[i], len);
		x.close(&gendata[i - Settings::HashSize]);
	}

	Shabal256_SSE2 x;
	x.update(&gendata[0], 16 + Settings::PlotSize);
	x.close(&final[0]);

	// XOR with final
	for (size_t i = 0; i < Settings::PlotSize; i++)
		gendata[i] ^= final[i % Settings::HashSize];

	return gendata;
}

Poco::UInt64 Burst::PlotGenerator::check(const std::vector<char>& buffer, const Miner& miner)
{
	std::array<uint8_t, 32> target{};
	Poco::UInt64 result;

	const auto generationSignature = miner.getGensig();
	const auto scoop = miner.getScoopNum();
	const auto basetarget = miner.getBaseTarget();

	Shabal256_SSE2 y;
	y.update(generationSignature.data(), Settings::HashSize);
	y.update(&buffer[scoop * Settings::ScoopSize], Settings::ScoopSize);
	y.close(target.data());

	memcpy(&result, target.data(), sizeof(Poco::UInt64));

	return result / basetarget;
}

Poco::UInt64 Burst::PlotGenerator::generateAndCheck(Poco::UInt64 account, Poco::UInt64 nonce, const Miner& miner)
{
	const auto buffer = generate(account, nonce);
	return check(buffer, miner);
}

float Burst::PlotGenerator::checkPlotfileIntegrity(const std::string& plotPath, Miner& miner)
{
	const PlotFile plotFile{std::string{plotPath}, 0};
	const auto account = plotFile.getAccountId();
	const auto startNonce = plotFile.getNonceStart();
	const auto nonceCount = plotFile.getNonces();
	const auto staggerSize = plotFile.getStaggerSize();

	std::random_device rd;
	std::mt19937 gen(rd());
	const std::uniform_int_distribution<Poco::UInt64> randInt(0, nonceCount);

	log_information(MinerLogger::general, "Checking file " + plotPath + " for corruption ...");

	float totalIntegrity = 0;
	const auto checkNonces = 30; //number of nonces to check
	const auto checkScoops = 32; //number of scoops to check per nonce
	auto noncesChecked = 0; //counter for the case of nonceCount not devisible by checkNonces

	for (auto nonceInterval = startNonce; nonceInterval < startNonce + nonceCount; nonceInterval += nonceCount /
	     checkNonces)
	{
		while (miner.isProcessing())
			Poco::Thread::sleep(1000);

		auto nonce = nonceInterval + randInt(gen) % (nonceCount / checkNonces);

		if (nonce >= startNonce + nonceCount)
			nonce = startNonce + nonceCount - 1;

		const auto gendata = generate(account, nonce);

		std::ifstream plotFileStream(plotPath, std::ifstream::in | std::ifstream::binary);

		const auto scoopSize = Settings::ScoopSize;
		const auto nonceStaggerOffset = (nonce - startNonce) / staggerSize * Settings::PlotSize * staggerSize + (nonce -
			startNonce) % staggerSize * scoopSize;
		const auto nonceScoopOffset = staggerSize * scoopSize;

		char readNonce[16 + Settings::PlotSize];
		char buffer[scoopSize];
		Poco::UInt64 scoopsIntact = 0;
		Poco::UInt64 scoopsChecked = 0;

		const auto scoopStep = Settings::ScoopPerPlot / checkScoops;

		// read scoops from nonce and compare to generated nonce
		for (size_t scoopInterval = 0; scoopInterval < Settings::ScoopPerPlot; scoopInterval += scoopStep)
		{
			auto scoop = scoopInterval + randInt(gen) % scoopStep;

			if (scoop >= Settings::ScoopPerPlot)
				scoop = Settings::ScoopPerPlot - 1;

			plotFileStream.seekg(nonceStaggerOffset + scoop * nonceScoopOffset);
			plotFileStream.read(buffer, scoopSize);
			Poco::UInt64 bytesIntact = 0;

			for (size_t byte = 0; byte < scoopSize; byte++)
			{
				readNonce[scoop * scoopSize + byte] = buffer[byte];
				if (readNonce[scoop * scoopSize + byte] == gendata[scoop * scoopSize + byte])
					bytesIntact++;
			}

			if (bytesIntact == scoopSize)
				scoopsIntact++;

			scoopsChecked++;
		}

		//calculate and output of integrity of this nonce
		const auto intact = static_cast<float>(scoopsIntact) / scoopsChecked * 100;
		log_information(MinerLogger::general, "Nonce %Lu: %f% intact", nonce, intact);
		totalIntegrity += intact;
		noncesChecked++;
		plotFileStream.close();
	}

	const auto integrity = totalIntegrity / noncesChecked;
	log_information(MinerLogger::general, "Total Integrity: %f%");
	return integrity;
}
