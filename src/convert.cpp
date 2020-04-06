asset sx::convert_out( const name owner, const asset quantity, const symbol_code symcode )
{
    require_auth( owner );

    // calculate pool fee (taken from inflow quantity)
    const asset pool_fee = calculate_pool_fee( quantity );

    // validate input
    check_min_convert( quantity );
    check_max_pool_ratio( quantity );

    // final out price
    const asset out_quantity = quantity - pool_fee;
    check( out_quantity.amount > 0, "quantity must be higher");
    const asset out = calculate_out( out_quantity, symcode );

    // validate output
    check_min_pool_ratio( out );

    // add converting fee to proceeds
    add_proceeds( pool_fee );

    // update volume
    add_volume( quantity, pool_fee );
    add_volume( out, asset{ 0, out.symbol } );

    // send transfer
    token::transfer_action transfer( get_contract( out.symbol.code() ), { get_self(), "active"_n });
    transfer.send( get_self(), owner, out, "convert" );

    return out;
}

double sx::get_ratio( const symbol_code symcode )
{
    // calculate ratio between depth & balance
    auto pool = _pools.get( symcode.raw(), "[symcode] pool does not exist");
    const asset balance = token::get_balance( pool.id.get_contract(), get_self(), symcode ) - pool.proceeds;
    return static_cast<double>(balance.amount) / pool.depth.amount;
}

void sx::check_min_convert( const asset quantity )
{
    const asset min_convert = _settings.get().min_convert;
    const auto pool = _pools.get( quantity.symbol.code().raw(), "[check_min_convert::quantity] pool does not exists");
    const double pegged = pool.type == symbol_code{"USD"} ? 1.00 : asset_to_double( pool.pegged );

    check( asset_to_double( quantity ) * pegged >= asset_to_double( min_convert ), "[quantity] must exceed minimum convert amount of " + min_convert.to_string());
}


void sx::check_max_pool_ratio( const asset quantity )
{
    // validate input
    auto pool = _pools.get( quantity.symbol.code().raw(), "[symcode] pool does not exist");
    const asset remaining = pool.balance + quantity;

    check( static_cast<double>(remaining.amount) / pool.depth.amount <= 5, quantity.symbol.code().to_string() + " pool ratio must be lower than 500%" );
}

void sx::check_min_pool_ratio( const asset out )
{
    // validate input
    auto pool = _pools.get( out.symbol.code().raw(), "[symcode] pool does not exist");
    const asset remaining = pool.balance - out;

    check( static_cast<double>(remaining.amount) / pool.depth.amount >= 0.2, out.symbol.code().to_string() + " pool ratio must be above 20%" );
}

asset sx::calculate_pool_fee( const asset quantity )
{
    const auto settings = _settings.get();

    // fee colleceted from incoming transfer (in basis points 1/100 of 1% )
    asset calculated_fee = quantity * settings.pool_fee / 100'00;

    // set minimum fee to smallest decimal of asset
    if ( settings.pool_fee > 0 && calculated_fee.amount == 0 ) calculated_fee.amount = 1;
    check( calculated_fee < quantity, "fee exceeds quantity");
    return calculated_fee;
}

asset sx::calculate_out( const asset quantity, const symbol_code symcode )
{
    const auto settings = _settings.get();
    const symbol_code base_symcode = quantity.symbol.code();
    const symbol_code quote_symcode = symcode;
    check( base_symcode != quote_symcode, symcode.to_string() + " cannot convert symbol code to self");
    check( quantity.symbol.raw() != 0, "[quantity] cannot be empty");
    check( symcode.raw() != 0, "[symcode] cannot be empty");

    // pools
    auto base = _pools.find( base_symcode.raw() );
    auto quote = _pools.find( quote_symcode.raw() );
    const symbol quote_sym = quote->id.get_symbol();

    // pegged
    const double base_pegged = asset_to_double( base->pegged );
    const double quote_pegged = asset_to_double( quote->pegged );

    // asserts
    check( base != _pools.end(), base_symcode.to_string() + " pool does not exist" );
    check( quote != _pools.end(), quote_symcode.to_string() + " pool does not exist" );
    check( base->balance.amount != 0, base_symcode.to_string() + " pool has no balance" );
    check( quote->balance.amount != 0, quote_symcode.to_string() + " pool has no balance" );
    check( base->depth.amount != 0, base_symcode.to_string() + " pool has no depth" );
    check( quote->depth.amount != 0, quote_symcode.to_string() + " pool has no depth" );
    check( base->connectors.find( quote_symcode )->raw() == quote_symcode.raw(), quote_symcode.to_string() + " connector does not exist" );
    check( base->enabled, base_symcode.to_string() + " pool is not enabled" );
    check( quote->enabled, quote_symcode.to_string() + " pool is not enabled" );

    // depth
    const double base_depth = asset_to_double( base->depth ) * base_pegged;
    const double quote_depth = asset_to_double( quote->depth ) * quote_pegged;
    const double min_depth = std::min( base_depth, quote_depth );

    // min amplifier
    const int64_t min_amplifier = std::min( base->amplifier, quote->amplifier );

    // ratio
    const double base_ratio = static_cast<double>(base->balance.amount) / base->depth.amount;
    const double quote_ratio = static_cast<double>(quote->balance.amount) / quote->depth.amount;

    // upper
    const double base_upper = ( min_amplifier * min_depth - min_depth + (min_depth * base_ratio));
    const double quote_upper = ( min_amplifier * min_depth - min_depth + (min_depth * quote_ratio));

    // bancor
    // amount / (balance_from + amount) * balance_to
    const double in_amount = asset_to_double( quantity ) * base_pegged;
    const double out_amount = in_amount / ( base_upper + in_amount ) * quote_upper;

    // print("\nbase_symcode: " + base_symcode.to_string() + "\n");
    // print("quote_symcode: " + quote_symcode.to_string() + "\n");
    // print("min_amplifier: " + to_string( min_amplifier ) + "\n");
    // print("base_pegged: " + to_string( base_pegged ) + "\n");
    // print("quote_pegged: " + to_string( quote_pegged ) + "\n");
    // print("base->balance.amount: " + to_string( base->balance.amount ) + "\n");
    // print("quote->balance.amount: " + to_string( quote->balance.amount ) + "\n");
    // print("base_depth: " + to_string( base_depth ) + "\n");
    // print("quote_depth: " + to_string( quote_depth ) + "\n");
    // print("base_ratio: " + to_string( base_ratio ) + "\n");
    // print("quote_ratio: " + to_string( quote_ratio ) + "\n");
    // print("base_upper: " + to_string( base_upper ) + "\n");
    // print("quote_upper: " + to_string( quote_upper ) + "\n");
    // print("out_amount: " + to_string( out_amount ) + "\n");
    // print("in_amount: " + to_string( in_amount ) + "\n");

    return double_to_asset( out_amount / quote_pegged, quote_sym );
}