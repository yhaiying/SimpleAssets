/**
 *  SimpleAssets (Digital Assets)
 *  (C) 2019 by CryptoLions [ https://CryptoLions.io ]
 *
 *  A simple standard for digital assets (ie. Non-Fungible Tokens) for EOSIO blockchains
 *
 *    WebSite:        https://simpleassets.io
 *    GitHub:         https://github.com/CryptoLions/SimpleAssets 
 *    Presentation:   https://medium.com/@cryptolions/introducing-simple-assets-b4e17caafaa4
 *
 *    Event Receiver: https://github.com/CryptoLions/SimpleAssets-EventReceiverExample
 * 
 */

 
#include <SimpleAssets.hpp>

ACTION SimpleAssets::updatever( string version ) {
	require_auth(get_self());
	
	Configs configs(_self, _self.value);
	configs.set(tokenconfigs{"simpleassets"_n, version}, _self);	
}

ACTION SimpleAssets::regauthor( name author, string data, string stemplate) {

	require_auth( author );
	require_recipient( author );

	check( data.size() > 3, "Data field is too short. Please tell us about yourselves." );	
	
	authors author_(_self, _self.value);
	auto itr = author_.find( author.value );

	if (itr == author_.end()){
		author_.emplace( author, [&]( auto& s ) {     
			s.author = author;
			s.data = data;	
			s.stemplate = stemplate;
		});
		
		return;
	}
	
	check (false, "Registration Error. You're probably already registered. Try the authupdate action.");
}


ACTION SimpleAssets::authorupdate( name author, string data, string stemplate) {
	require_auth( author );
	require_recipient( author );

	authors author_(_self, _self.value);
	auto itr = author_.find( author.value );

	check ( itr != author_.end(), "author not registered" );
	
	if (data == "" && stemplate == "") {
		itr = author_.erase(itr);
	} else {
		author_.modify( itr, author, [&]( auto& s ) {
			s.data = data;	
			s.stemplate = stemplate;		
		});
	}
}

// Non-Fungible Token Logic

ACTION SimpleAssets::create( name author, name category, name owner, string idata, string mdata, bool requireclaim) {

	require_auth( author );
	check( is_account( owner ), "owner account does not exist");

	require_recipient( owner );
	
	uint64_t newID = getid(false);
	
	name assetOwner = owner;
	
	check (!(author.value == owner.value && requireclaim == 1), "Can't requireclaim if author == owner.");
	
	if (requireclaim){
		assetOwner = author;
		//add info to offers table
		offers offert(_self, _self.value);
		offert.emplace( author, [&]( auto& s ) {     
			s.assetid = newID;
			s.offeredto = owner;
			s.owner = author;
			s.cdate = now();
		});
	}
	
	sassets assets(_self, assetOwner.value);
	assets.emplace( author, [&]( auto& s ) {     
		s.id = newID;
		s.owner = assetOwner;
		s.author = author;
		s.category = category;
		s.mdata = mdata; // mutable data
		s.idata = idata; // immutable data
	});
	
	//Events
	sendEvent(author, author, "saecreate"_n, std::make_tuple(owner, newID));
	SEND_INLINE_ACTION( *this, createlog, { {_self, "active"_n} },  { author, category, owner, idata, mdata, newID, requireclaim}   );
}


ACTION SimpleAssets::createlog( name author, name category, name owner, string idata, string mdata, uint64_t assetid, bool requireclaim) {
	require_auth(get_self());
}


ACTION SimpleAssets::claim( name claimer, std::vector<uint64_t>& assetids) {
	require_auth( claimer );
	require_recipient( claimer );
	
	offers offert(_self, _self.value);
	sassets assets_t(_self, claimer.value);
	
	std::map< name, std::map< uint64_t, name > > uniqauthor;
	for( size_t i = 0; i < assetids.size(); ++i ) {

		auto itrc = offert.find( assetids[i] );

		check(itrc != offert.end(), "Cannot find at least one of the assets you're attempting to claim.");
		check(claimer == itrc->offeredto, "At least one of the assets has not been offerred to you.");

		sassets assets_f( _self, itrc->owner.value );
		auto itr = assets_f.find( assetids[i] );
		check(itr != assets_f.end(), "Cannot find at least one of the assets you're attempting to claim.");

		check(itrc->owner.value == itr->owner.value, "Owner was changed for at least one of the items!?");   

		assets_t.emplace( claimer, [&]( auto& s ) {     
			s.id = itr->id;
			s.owner = claimer;
			s.author = itr->author;
			s.category = itr->category;		
			s.mdata = itr->mdata; 		// mutable data
			s.idata = itr->idata; 		// immutable data
			s.container = itr->container;
			s.containerf = itr->containerf;
		});

		assets_f.erase(itr);
		offert.erase(itrc);

		//Events
		uniqauthor[itr->author][assetids[i]] = itrc->owner;
	}

	//Send Event as deferred	
	auto uniqauthorIt = uniqauthor.begin(); 
	while(uniqauthorIt != uniqauthor.end() ) {
		name keyauthor = (*uniqauthorIt).first; 
		sendEvent(keyauthor, claimer, "saeclaim"_n, std::make_tuple(claimer, uniqauthor[keyauthor]));
		uniqauthorIt++;
	}
}


ACTION SimpleAssets::transfer( name from, name to, std::vector<uint64_t>& assetids, string memo){
	
	check( from != to, "cannot transfer to yourself" );
	check( is_account( to ), "TO account does not exist");
	check( memo.size() <= 256, "memo has more than 256 bytes" );	
		
	require_recipient( from );
	require_recipient( to );
		
	sassets assets_f( _self, from.value );
	sassets assets_t(_self, to.value);
	
	delegates delegatet(_self, _self.value);
	offers offert(_self, _self.value);

	auto rampayer = has_auth( to ) ? to : from;
		
	bool isDelegeting = false;
	
	std::map< name, std::vector<uint64_t> > uniqauthor;
	
	for( size_t i = 0; i < assetids.size(); ++i ) {
		auto itrd = delegatet.find( assetids[i] );

		isDelegeting = false;
		if (itrd != delegatet.end()){
			auto dg = *itrd;
			if (itrd->owner == to || itrd->delegatedto == to){
				isDelegeting = true;		
				if (itrd->owner == to)
					delegatet.erase(itrd);
			} else {
				check ( false, "At least one of the assets cannot be transferred because it is delegated" );	
			}
		}
		
		if (isDelegeting){
			require_auth( has_auth( itrd->owner  ) ? itrd->owner  : from);
			
		} else {
			require_auth( from );
		}

	
		auto itr = assets_f.find( assetids[i] );
		check(itr != assets_f.end(), "At least one of the assets cannot be found (check ids?)");

		check(from.value == itr->owner.value, "At least one of the assets is not yours to transfer.");   

		auto itrc = offert.find( assetids[i] );
		check ( itrc == offert.end(), "At least one of the assets has been offered for a claim and cannot be transferred. Cancel offer?" );

		assets_f.erase(itr);
	
		assets_t.emplace( rampayer, [&]( auto& s ) {     
			s.id = itr->id;
			s.owner = to;
			s.author = itr->author;
			s.category = itr->category;		
			s.idata = itr->idata; 		// immutable data
			s.mdata = itr->mdata; 		// mutable data
			s.container = itr->container;
			s.containerf = itr->containerf;

		});
		
		//Events
		uniqauthor[itr->author].push_back(assetids[i]);		
	}
	
	//Send Event as deferred
	auto uniqauthorIt = uniqauthor.begin(); 
	while(uniqauthorIt != uniqauthor.end() ) {
		name keyauthor = (*uniqauthorIt).first; 
		sendEvent(keyauthor, rampayer, "saetransfer"_n, std::make_tuple(from, to, uniqauthor[keyauthor], memo) );
		uniqauthorIt++;
	}
}


ACTION SimpleAssets::update( name author, name owner, uint64_t assetid, string mdata ) {
	
	require_auth( author );

	sassets assets_f( _self, owner.value );

	auto itr = assets_f.find( assetid );
	check(itr != assets_f.end(), "asset not found");

	check(itr->author == author, "Only author can update asset.");
	
	assets_f.modify( itr, author, [&]( auto& a ) {
        a.mdata = mdata;
    });
}


ACTION SimpleAssets::offer( name owner, name newowner, std::vector<uint64_t>& assetids, string memo){

	check( owner != newowner, "cannot offer to yourself" );
	
	require_auth( owner );
	require_recipient( owner );
	require_recipient( newowner );
	
	check( is_account( newowner ), "newowner account does not exist");
	
	sassets assets_f( _self, owner.value );
	offers offert(_self, _self.value);
	delegates delegatet(_self, _self.value);	
		
	for( size_t i = 0; i < assetids.size(); ++i ) {
		auto itr = assets_f.find( assetids[i] );
		check(itr != assets_f.end(), "At least one of the assets was not found.");

		auto itrc = offert.find( assetids[i] );
		check ( itrc == offert.end(), "At least one of the assets is already offered for claim." );

		auto itrd = delegatet.find( assetids[i] );
		check ( itrd == delegatet.end(), "At least one of the assets is delegated and cannot be offered." );
		
		offert.emplace( owner, [&]( auto& s ) {     
			s.assetid = assetids[i];
			s.offeredto = newowner;
			s.owner = owner;
			s.cdate = now();
		});
	}
}


ACTION SimpleAssets::canceloffer( name owner, std::vector<uint64_t>& assetids){

	require_auth( owner );
	require_recipient( owner );
	
	offers offert(_self, _self.value);

	for( size_t i = 0; i < assetids.size(); ++i ) {
		auto itr = offert.find( assetids[i] );

		check ( itr != offert.end(), "The offer for at least one of the assets was not found." );
		check (owner.value == itr->owner.value, "You're not the owner of at least one of the assets whose offers you're attempting to cancel.");

		offert.erase(itr);
	}
}


ACTION SimpleAssets::burn( name owner, std::vector<uint64_t>& assetids, string memo ) {

	require_auth( owner );	

	sassets assets_f( _self, owner.value );
	offers offert(_self, _self.value);
	delegates delegatet(_self, _self.value);
		
	std::map< name, std::vector<uint64_t> > uniqauthor;
	
	for( size_t i = 0; i < assetids.size(); ++i ) {
		
		auto itr = assets_f.find( assetids[i] );
		check(itr != assets_f.end(), "At least one of the assets was not found.");

		check(owner.value == itr->owner.value, "At least one of the assets you're attempting to burn is not yours.");

		auto itrc = offert.find( assetids[i] );
		check ( itrc == offert.end(), "At least one of the assets has an open offer and cannot be burned." );

		auto itrd = delegatet.find( assetids[i] );
		check ( itrd == delegatet.end(), "At least one of assets is delegated and cannot be burned." );
		
		assets_f.erase(itr);
		
		//Events
		uniqauthor[itr->author].push_back(assetids[i]);
	}
	
	//Send Event as deferred
	auto uniqauthorIt = uniqauthor.begin(); 
	while(uniqauthorIt != uniqauthor.end() ) {
		name keyauthor = (*uniqauthorIt).first; 
		sendEvent(keyauthor, owner, "saeburn"_n, std::make_tuple(owner, uniqauthor[keyauthor], memo));
		uniqauthorIt++;
	}
}


ACTION SimpleAssets::delegate( name owner, name to, std::vector<uint64_t>& assetids, uint64_t period, string memo ){

	check( owner != to, "cannot delegate to yourself" );

	require_auth( owner );
	require_recipient( owner );
	
	check( is_account( to ), "TO account does not exist");

	sassets assets_f( _self, owner.value );
	delegates delegatet(_self, _self.value);
	offers offert(_self, _self.value);

	string assetidsmemo = "";
	
	for( size_t i = 0; i < assetids.size(); ++i ) {
		
		auto itr = assets_f.find( assetids[i] );
		check(itr != assets_f.end(), "At least one of the assets cannot be found.");
		

		auto itrd = delegatet.find( assetids[i] );
		check ( itrd == delegatet.end(), "At least one of the assets is already delegated." );

		auto itro = offert.find( assetids[i] );
		check ( itro == offert.end(), "At least one of the assets has an open offer and cannot be delegated." );
		
		delegatet.emplace( owner, [&]( auto& s ) {     
			s.assetid = assetids[i];
			s.owner = owner;
			s.delegatedto = to;
			s.cdate = now();
			s.period = period;
		});
		
	}
	string newmemo = "Delegate memo: "+memo;
	SEND_INLINE_ACTION( *this, transfer, { {owner, "active"_n} },  { owner, to, assetids, newmemo}   );
}


ACTION SimpleAssets::undelegate( name owner, name from, std::vector<uint64_t>& assetids ){

	require_auth( owner );
	require_recipient( owner );
	
	check( is_account( from ), "to account does not exist");

	sassets assets_f( _self, from.value );
	delegates delegatet(_self, _self.value);

	string assetidsmemo = "";	
	for( size_t i = 0; i < assetids.size(); ++i ) {
		
		auto itr = assets_f.find( assetids[i] );
		check(itr != assets_f.end(), "At least one of the assets cannot be found.");

		auto itrc = delegatet.find( assetids[i] );
		check ( itrc != delegatet.end(), "At least one of the assets is not delegated." );

		check(owner == itrc->owner, "You are not the owner of at least one of these assets.");
		check(from == itrc->delegatedto, "FROM does not match DELEGATEDTO for at least one of the assets.");   
		check(itr->owner == itrc->delegatedto, "FROM does not match DELEGATEDTO for at least one of the assets.");   		
		check( (itrc->cdate + itrc->period) < now(), "Cannot undelegate until the PERIOD expires.");   		
			
		if (i != 0) assetidsmemo += ", ";
		assetidsmemo += std::to_string(assetids[i]);
	}
	
	SEND_INLINE_ACTION( *this, transfer, { {owner, "active"_n} },  { from, owner, assetids, "undelegate assetid: "+assetidsmemo }   );
}


ACTION SimpleAssets::attach( name owner, uint64_t assetidc, std::vector<uint64_t>& assetids ){

	sassets assets_f( _self, owner.value );
	delegates delegatet(_self, _self.value);
	offers offert(_self, _self.value);

	require_recipient( owner );

	auto ac_ = assets_f.find( assetidc );
	check(ac_ != assets_f.end(), "Asset cannot be found.");
	const auto& ac = *ac_;

	require_auth( ac_->author );
	
	for( size_t i = 0; i < assetids.size(); ++i ) {
		
		auto itr = assets_f.find( assetids[i] );
		check(itr != assets_f.end(), "At least one of the assets cannot be found.");

		check(assetidc != assetids[i], "Cannot attcach to self.");

		const auto& itr_ = *itr;	

		check(itr_.author == ac.author, "Different authors.");
		auto itrd = delegatet.find( assetids[i] );
		check ( itrd == delegatet.end(), "At least one of the assets is delegated." );

		auto itro = offert.find( assetids[i] );
		check ( itro == offert.end(), "At least one of the assets has an open offer and cannot be delegated." );

		assets_f.modify( ac_, ac.author, [&]( auto& a ) {				
			a.container.push_back(itr_);
		});
		assets_f.erase(itr);
	}
}


ACTION SimpleAssets::detach( name owner, uint64_t assetidc, std::vector<uint64_t>& assetids ){
	
	require_auth( owner );
	require_recipient( owner );

	sassets assets_f( _self, owner.value );
		
	auto ac_ = assets_f.find( assetidc );
	check(ac_ != assets_f.end(), "Asset cannot be found.");
	const auto& ac = *ac_;
		
	for( size_t i = 0; i < assetids.size(); ++i ) {
		std::vector<sasset> newcontainer;
		
		for( size_t j = 0; j < ac.container.size(); ++j ) {
			auto acc = ac.container[j];
			if ( assetids[i] == acc.id){
				 
				assets_f.emplace( owner, [&]( auto& s ) {     
					s.id = acc.id;
					s.owner = acc.owner;
					s.author = acc.author;
					s.category = acc.category;		
					s.idata = acc.idata; 		// immutable data
					s.mdata = acc.mdata; 		// mutable data
					s.container = acc.container;
					s.containerf = acc.containerf;
				});
				
			} else {
				newcontainer.push_back(acc);
			}		
		}
		
		assets_f.modify( ac_, owner, [&]( auto& a ) {
			a.container = newcontainer;
		});
	}	
}


ACTION SimpleAssets::attachf( name owner, name author, asset quantity, uint64_t assetidc ){
	attachdeatch( owner, author, quantity, assetidc, true );
}


ACTION SimpleAssets::detachf( name owner, name author, asset quantity, uint64_t assetidc ){
	attachdeatch( owner, author, quantity, assetidc, false );
}
	

//-----------------------------------------------------------------
// Fungible Token Logic

ACTION SimpleAssets::createf( name author, asset maximum_supply, bool authorctrl, string data){

	require_auth( author );

	auto sym = maximum_supply.symbol;

	check( sym.is_valid(), "invalid symbol name" );
	check( maximum_supply.is_valid(), "invalid supply");
	check( maximum_supply.amount > 0, "max-supply must be positive");

	stats statstable( _self, author.value ); 

	auto existing = statstable.find( sym.code().raw() );
	check( existing == statstable.end(), "token with symbol already exists" );

	statstable.emplace( author, [&]( auto& s ) {
		s.supply.symbol	= maximum_supply.symbol;
		s.max_supply	= maximum_supply;
		s.issuer		= author;
		s.id			= getid(false);
		s.authorctrl	= authorctrl;
		s.data			= data;
	});
}


ACTION SimpleAssets::updatef( name author, symbol sym, string data){
	require_auth( author );

    check( sym.is_valid(), "invalid symbol name" );

    stats statstable( _self, author.value ); 
    auto existing = statstable.find( sym.code().raw() );
    check( existing != statstable.end(), "Symbol not exists" );
	
	statstable.modify( existing, author, [&]( auto& a ) {
        a.data = data;
    });
}


ACTION SimpleAssets::issuef( name to, name author, asset quantity, string memo ){
	
	auto sym = quantity.symbol;
	check( sym.is_valid(), "invalid symbol name" );
	check( memo.size() <= 256, "memo has more than 256 bytes" );

	stats statstable( _self, author.value );
	auto existing = statstable.find( sym.code().raw() );
	check( existing != statstable.end(), "token with symbol does not exist, create token before issue" );
	const auto& st = *existing;

	require_auth( st.issuer );
	check( quantity.is_valid(), "invalid quantity" );
	check( quantity.amount > 0, "must issue positive quantity" );

	check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
	check( quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

	statstable.modify( st, same_payer, [&]( auto& s ) {
		s.supply += quantity;
	});

	add_balancef( st.issuer, author, quantity, st.issuer );

	if( to != st.issuer ) {
		SEND_INLINE_ACTION( *this, transferf, { {st.issuer, "active"_n} },
			{ st.issuer, to, author, quantity, memo }
		);
	}	
}


ACTION SimpleAssets::transferf( name from, name to, name author, asset quantity, string memo ){
	
	check( from != to, "cannot transfer to self" );

	check( is_account( to ), "to account does not exist");
	auto sym = quantity.symbol.code();
	stats statstable( _self, author.value );
	const auto& st = statstable.get( sym.raw() );

	require_recipient( from );
	require_recipient( to );

	check( quantity.is_valid(), "invalid quantity" );
	check( quantity.amount > 0, "must transfer positive quantity" );
	check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
	check( memo.size() <= 256, "memo has more than 256 bytes" );

	auto payer = has_auth( to ) ? to : from;
	auto checkAuth = from;

	if (st.authorctrl &&  has_auth( st.issuer )){
		checkAuth = st.issuer;
		payer = st.issuer;
	}

	require_auth( checkAuth );	

	sub_balancef( from, author, quantity );
	add_balancef( to, author, quantity, payer );
}


ACTION SimpleAssets::offerf( name owner, name newowner, name author, asset quantity, string memo){

	require_auth( owner );
	require_recipient( owner );
	require_recipient( newowner );

	check( is_account( newowner ), "newowner account does not exist");
	check( owner != newowner, "cannot offer to yourself" );

	auto sym = quantity.symbol;
	check( sym.is_valid(), "invalid symbol name" );
	check( memo.size() <= 256, "memo has more than 256 bytes" );

	stats statstable( _self, author.value );
	auto existing = statstable.find( sym.code().raw() );
	check( existing != statstable.end(), "token with symbol does not exist" );
	const auto& st = *existing;
		
	check( quantity.is_valid(), "invalid quantity" );
	check( quantity.amount > 0, "must retire positive quantity" );

	check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );

	offerfs offert(_self, _self.value);
	auto owner_index = offert.template get_index<"owner"_n>();
	auto itro = owner_index.find( owner.value );

	for (; itro != owner_index.end(); itro++) {
		check(  !(itro->author == author && itro->offeredto == newowner && itro->quantity.symbol == quantity.symbol ), "Such an offer already exists");		
	}
	
	offert.emplace( owner, [&]( auto& s ) {     
		s.id = getid(true);
		s.author = author;
		s.quantity = quantity; 
		s.offeredto = newowner;
		s.owner = owner;
		s.cdate = now();
	});
	sub_balancef( owner, author, quantity );
}


ACTION SimpleAssets::cancelofferf( name owner, std::vector<uint64_t>& ftofferids){
	require_auth( owner );
	require_recipient( owner );
	
	offerfs offert(_self, _self.value);

	for( size_t i = 0; i < ftofferids.size(); ++i ) {

		uint64_t offtid = ftofferids[i];
		auto itr = offert.find( offtid );

		check ( itr != offert.end(), "The offer for at least one of the FT was not found." );
		check (owner.value == itr->owner.value, "You're not the owner of at least one of those FTs.");

		add_balancef( owner, itr->author, itr->quantity, owner );
		offert.erase(itr);
	}	
}

ACTION SimpleAssets::claimf( name claimer, std::vector<uint64_t>& ftofferids) {

	require_auth( claimer );
	require_recipient( claimer );
	
	offerfs offert(_self, _self.value);
	
	std::map< name, std::vector<uint64_t> > uniqauthor;
		
	for( size_t i = 0; i < ftofferids.size(); ++i ) {
		uint64_t offtid = ftofferids[i];
	
		auto itrc = offert.find( offtid );

		check(itrc != offert.end(), "Cannot find at least one of the FT you're attempting to claim.");
		check(claimer == itrc->offeredto, "At least one of the FTs has not been offerred to you.");

		add_balancef( claimer, itrc->author, itrc->quantity, claimer );
		offert.erase(itrc);
	}
}


ACTION SimpleAssets::burnf( name from, name author, asset quantity, string memo ){

	auto sym = quantity.symbol;
	check( sym.is_valid(), "invalid symbol name" );
	check( memo.size() <= 256, "memo has more than 256 bytes" );

	stats statstable( _self, author.value );
	auto existing = statstable.find( sym.code().raw() );
	check( existing != statstable.end(), "token with symbol does not exist" );
	const auto& st = *existing;

	require_auth( st.authorctrl && has_auth( st.issuer ) ? st.issuer  : from);
		
	check( quantity.is_valid(), "invalid quantity" );
	check( quantity.amount > 0, "must retire positive quantity" );

	check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );

	statstable.modify( st, same_payer, [&]( auto& s ) {
		s.supply -= quantity;
	});

	sub_balancef( from, author, quantity );
}


ACTION SimpleAssets::openf( name owner, name author, const symbol& symbol, name ram_payer ){
	
	require_auth( ram_payer );

	auto sym_code_raw = symbol.code().raw();

	stats statstable( _self, author.value );
	const auto& st = statstable.get( sym_code_raw, "symbol does not exist" );
	check( st.supply.symbol == symbol, "symbol precision mismatch" );

	accounts acnts( _self, owner.value );

	uint64_t ftid = st.id; 

	auto it = acnts.find( ftid );
	if( it == acnts.end() ) {
		acnts.emplace( ram_payer, [&]( auto& a ){
			a.id = st.id;
			a.author = author;
			a.balance = asset{0, symbol};
		});
	}
}


ACTION SimpleAssets::closef( name owner, name author, const symbol& symbol ){
	require_auth( owner );
	accounts acnts( _self, owner.value );
	
	uint64_t ftid = getFTIndex(author, symbol); 
	
	auto it = acnts.find( ftid );
	check( it != acnts.end(), "Balance row already deleted or never existed. Action won't have any effect." );
	check( it->balance.amount == 0, "Cannot close because the balance is not zero." );
	
	offerfs offert(_self, _self.value);
	auto owner_index = offert.template get_index<"owner"_n>();
	auto itro = owner_index.find( owner.value );
	for (; itro != owner_index.end(); itro++) {
		check(  !(itro->author == author && itro->quantity.symbol == symbol ), "You have open offers for this FT..");		
	}
	
	acnts.erase( it );
}



//-------------------------------------------------------------------------------------
//------------- PRIVATE ---------------------------------------------------------------

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
/*
* getid private action
* Increment, save and return id for a new asset or new fungible token.
*/
uint64_t SimpleAssets::getid(bool defer){

	conf config(_self, _self.value);
	_cstate = config.exists() ? config.get() : global{};

	uint64_t resid;
	if (defer) {
		_cstate.defid++;
		resid = _cstate.defid;
	} else {
		_cstate.lnftid++;
		resid = _cstate.lnftid;
	}

	config.set(_cstate, _self);
	return resid;
}


uint64_t SimpleAssets::getFTIndex(name author, symbol symbol){

	stats statstable( _self, author.value );

	auto existing = statstable.find( symbol.code().raw() );
	check( existing != statstable.end(), "token with symbol does not exist." );
	const auto& st = *existing;

	uint64_t res =  st.id;

	return res;
}

void SimpleAssets::attachdeatch( name owner, name author, asset quantity, uint64_t assetidc, bool attach ){	

	sassets assets_f( _self, owner.value );
	delegates delegatet(_self, _self.value);
	offers offert(_self, _self.value);
	stats statstable( _self, author.value );

	auto sym = quantity.symbol.code();
	const auto& st = statstable.get( sym.raw() );

	require_recipient( owner );

	check( quantity.is_valid(), "invalid quantity" );
	check( quantity.amount > 0, "must transfer positive quantity" );
	check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );

	check( st.issuer == author, "Different authors");

	if (attach) {
		require_auth( author );	 //attach
	} else {
		require_auth( owner );  //deatach
	}

	auto itr = assets_f.find( assetidc );
	check(itr != assets_f.end(), "assetid cannot be found.");
	
	const auto& ac = *itr;	
	
	check(ac.author == author, "Different authors.");
	auto itrd = delegatet.find( assetidc );
	check ( itrd == delegatet.end(), "Asset is delegated." );

	auto itro = offert.find( assetidc );
	check ( itro == offert.end(), "Assets has an open offer and cannot be delegated." );

	std::vector<account> newcontainerf;

	bool found = false;
	
	for( size_t j = 0; j < ac.containerf.size(); j++ ) {
		auto accf = ac.containerf[j];
		if ( st.id == accf.id){
			if (attach) {
				accf.balance.amount += quantity.amount;
			} else {
				check( accf.balance.amount >= quantity.amount, "overdrawn balance" );   
				accf.balance.amount -= quantity.amount;  
			}
		
			found = true;
		} 
		if (accf.balance.amount > 0) 
			newcontainerf.push_back(accf);
	}
	
	if (!found && attach){
		account addft;
		addft.id = st.id;
		addft.author = author;
		addft.balance = quantity;

		newcontainerf.push_back(addft);
	}
	
	if (!attach)
		check (found, "not attached");   
	
	assets_f.modify( itr, author, [&]( auto& a ) {
		a.containerf = newcontainerf;
	});
		
	if (attach) {	
		sub_balancef( owner, author, quantity );
	} else {
		add_balancef( owner, author, quantity, owner ); 
	}
}

void SimpleAssets::sub_balancef( name owner, name author, asset value ) {

	accounts from_acnts( _self, owner.value );
	uint64_t ftid = getFTIndex(author, value.symbol);
	
	const auto& from = from_acnts.get( ftid, "no balance object found" );
	check( from.balance.amount >= value.amount, "overdrawn balance" );

	check( value.symbol.code().raw() == from.balance.symbol.code().raw(), "Wrong symbol");
	auto payer = has_auth( author ) ? author : owner;
	
	from_acnts.modify( from, payer, [&]( auto& a ) {
		a.balance -= value;
	});
}


void SimpleAssets::add_balancef( name owner, name author, asset value, name ram_payer ) {
	
	accounts to_acnts( _self, owner.value );
	
	uint64_t ftid = getFTIndex(author, value.symbol);
	auto to = to_acnts.find( ftid );
	
	if( to == to_acnts.end() ) {
		to_acnts.emplace( ram_payer, [&]( auto& a ){
			a.id = ftid;
			a.balance = value;
			a.author = author;
		});
	} else {
		to_acnts.modify( to, same_payer, [&]( auto& a ) {
			a.balance += value;
		});
	}
}

template<typename... Args>
void SimpleAssets::sendEvent(name author, name rampayer, name seaction, const std::tuple<Args...> &adata) {

	transaction sevent{};
	sevent.actions.emplace_back( permission_level{_self, "active"_n}, author, seaction, adata);
	sevent.delay_sec = 0;
	sevent.send(getid(true), rampayer);
}



//------------------------------------------------------------------------------------------------------------   

EOSIO_DISPATCH( SimpleAssets, 	(create)(createlog)(transfer)(burn)(update)
								(offer)(canceloffer)(claim)
								(regauthor)(authorupdate)
								(delegate)(undelegate)(attach)(detach)
								(createf)(updatef)(issuef)(transferf)(burnf)
								(offerf)(cancelofferf)(claimf)
								(attachf)(detachf)(openf)(closef)
								(updatever))

//============================================================================================================
//=======================================- SimpleAssets.io -==================================================
//======================================- by CryptoLions.io -=================================================
//============================================================================================================
