#include <consensus/tx_verify.h>
#include <uint256.h>
#include <map>

typedef std::map<uint256, int> BannedInputs;

const BannedInputs bannedFunds = {
	{
		{ uint256S("0xb19367635bed8605f99f9e9d9ce7093b6c0459563a4abe58a1038f9600d4d7a3"), 1 },
		{ uint256S("0x51eb6faffbd569ee95f888224561eb513dbe3a7669b84d5bb1d4a16788418f6c"), 1 },
		{ uint256S("0xd0f9360facbb364c65f6112011970a6980bcfcfbc48e6d9719912f7810ff50b6"), 0 },
		{ uint256S("0xd0f9360facbb364c65f6112011970a6980bcfcfbc48e6d9719912f7810ff50b6"), 1 },
    }
};

bool areBannedInputs(uint256 txid, int vout) {
	for (auto inputs : bannedFunds) {
		if (inputs.first == txid && inputs.second == vout) {
			return true;
		}
	}
	
	return false;
}
