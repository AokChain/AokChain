bool CTokensDB::TokenAddressDir(std::vector<std::pair<std::string, CAmount> >& vecAddressAmount, const std::string& tokenName)
{
    FlushStateToDisk();

    std::unique_ptr<CDBIterator> pcursor(NewIterator());
    pcursor->Seek(std::make_pair(TOKEN_ADDRESS_QUANTITY_FLAG, std::make_pair(tokenName, std::string())));

    size_t skip = 0;

    size_t loaded = 0;
    size_t offset = 0;

    size_t count = 1;

    // Load tokens
    while (pcursor->Valid() && loaded < count && loaded < MAX_DATABASE_RESULTS) {
        boost::this_thread::interruption_point();

        std::pair<char, std::pair<std::string, std::string> > key;
        if (pcursor->GetKey(key) && key.first == TOKEN_ADDRESS_QUANTITY_FLAG && key.second.first == tokenName) {

            CAmount amount;
            if (pcursor->GetValue(amount)) {
                vecAddressAmount.emplace_back(std::make_pair(key.second.second, amount));
                loaded += 1;
            } else {
                return error("%s: failed to Token Address Quanity", __func__);
            }

            pcursor->Next();
        } else {
            break;
        }
    }

    return true;
}