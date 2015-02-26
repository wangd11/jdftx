/*-------------------------------------------------------------------
Copyright 2011 Ravishankar Sundararaman

This file is part of JDFTx.

JDFTx is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

JDFTx is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with JDFTx.  If not, see <http://www.gnu.org/licenses/>.
-------------------------------------------------------------------*/

#include <commands/command.h>
#include <electronic/Everything.h>
#include <core/Units.h>
#include <config.h>

struct CommandIonSpecies : public Command
{
	CommandIonSpecies() : Command("ion-species")
	{
		format = "[<path>/]<id>[<suffix>].<format>\n"
			"\t  | [<path>/]$ID[<suffix>].<format>";
		comments =
			"Read pseudopotential from file [<path>/]<id>.<format>, which will be referred\n"
			"to internally by <id> in all other commands and in the output. Note that <id>\n"
			"is the start of the basename of the file, obtained by removing the path,\n"
			"extension and any suffix starting with non-aphanumeric characters eg.\n"
			"Br.fhi, ../Br.fhi and /home/foo/Br_theNotSoBadOne.uspp will all have <id> = Br.\n"
			"\n"
			"If the filename contains the string $ID, then this command specifies an\n"
			"entire set of pseudopotentials. Every time command ion encounters an otherwise\n"
			"undefined species, it will search for a pseudopotential file with this pattern\n"
			"(replacing $ID with the as yet undefined <id> needed by the ion command).\n"
			"If there are multiple such patterns, then they will be searched in the order\n"
			"that they appear in the input file.\n"
			"\n"
			"Currently supported <format>'s are:\n"
			"+ .fhi   ABINIT format FHI98 norm-conserving pseudopotentials (eg. generated by OPIUM).\n"
			"+ .uspp  Ultrasoft pseudopotentials generated by the USPP program (native binary format).\n"
			"+ .upf   Quantum Espresso Universal Pseudopotential Format (only the XML-like version 2).\n"
			"\n"
			"If [<path>/]<id>.pulay exists, pulay data (derivative of total energy with respect to\n"
			"number of planewaves per unit volume) will be read from that file. This is useful for\n"
			"lattice minimization at low cutoffs; see script calcPulay for generating such files.";
		allowMultiple = true;
	}

	void process(ParamList& pl, Everything& e)
	{	string filename; pl.get(filename, string(), "filename", true);
		if(filename.find("$ID")!=string::npos)
			e.iInfo.pspFilenamePatterns.push_back(filename); //set of psps (wildcard)
		else
			addSpecies(filename, e); //specific psp
	}
	
	static void addSpecies(string filename, Everything& e, bool fromWildcard=false)
	{	std::shared_ptr<SpeciesInfo> specie(new SpeciesInfo);
		specie->potfilename = filename;
		specie->fromWildcard = fromWildcard;
		
		//Split filename into segments:
		
		//--- check extension and get format:
		size_t lastDot = specie->potfilename.find_last_of(".");
		if(lastDot==string::npos)
			throw string("filename '" + specie->potfilename +"' does not have an extension from which format can be determined");
		string extension = specie->potfilename.substr(lastDot+1);
		if(extension=="fhi") specie->pspFormat = SpeciesInfo::Fhi;
		else if(extension=="uspp") specie->pspFormat = SpeciesInfo::Uspp;
		else if(extension=="upf") specie->pspFormat = SpeciesInfo::UPF;
		else throw string("unknown pseudopotential format extension '" + extension + "'");
		
		//--- remove leading path:
		specie->name = specie->potfilename.substr(0, lastDot); //Remove extension
		specie->pulayfilename = specie->name + ".pulay"; //Tentative pulay filename (note includes path)
		size_t lastSlash = specie->name.find_last_of("\\/");
		if(lastSlash != string::npos)
			specie->name = specie->name.substr(lastSlash+1);
		
		//--- remove suffix (if any)
		for(unsigned i=0; i<specie->name.length(); i++)
			if(!isalnum(specie->name[i]))
			{	specie->name = specie->name.substr(0,i);
				break;
			}
		
		//--- fix capitalization (for aesthetic purposes in output ionpos etc. (internally case-insensitive))
		specie->name[0] = toupper(specie->name[0]);

		//Check for a pulay file:
		if(!isReadable(specie->pulayfilename))
			specie->pulayfilename = "none"; //disable if such a file does not exist

		//Check for duplicates, add to the list:
		for(auto sp: e.iInfo.species)
			if(specie->name==sp->name)
				throw string("Ion species "+specie->name+" has been defined more than once");
		e.iInfo.species.push_back(specie);
	}

	void printStatus(Everything& e, int iRep)
	{	int iPrint=0;
		for(auto sp: e.iInfo.species)
			if(not sp->fromWildcard)
			{	if(iPrint==iRep) { logPrintf("%s", sp->potfilename.c_str()); return; }
				iPrint++;
			}
		for(string pattern: e.iInfo.pspFilenamePatterns)
		{	if(iPrint==iRep) { logPrintf("%s", pattern.c_str()); return; }
			iPrint++;
		}
	}
}
commandIonSpecies;

const std::vector<string>& getPseudopotentialPrefixes()
{	static std::vector<string> prefixes;
	if(!prefixes.size())
	{	prefixes.push_back(""); //search paths relative to current directory first
		prefixes.push_back(JDFTX_BUILD_DIR "/pseudopotentials/"); //search paths relative to the pseudopotential library root next
	}
	return prefixes;
}

std::shared_ptr<SpeciesInfo> findSpecies(string id, Everything& e)
{	//Initialize cache of available filenames for each wildcard
	static std::vector<std::vector<string> > validFilenames;
	const std::vector<string>& prefixes = getPseudopotentialPrefixes();
	if(validFilenames.size()<e.iInfo.pspFilenamePatterns.size())
	{	validFilenames.resize(e.iInfo.pspFilenamePatterns.size());
		for(size_t i=0; i<validFilenames.size(); i++)
		{	string pattern = e.iInfo.pspFilenamePatterns[i];
			pattern.replace(pattern.find("$ID"),3, "*");
			for(const string& prefix: prefixes)
			{	//Use ls to get a list of matching files:
				FILE* pp = popen(("ls " + prefix + pattern + " 2>/dev/null").c_str(), "r");
				const int bufLen=1024; char buf[bufLen];
				while(!feof(pp))
				{	fgets(buf, bufLen, pp);
					string fname(buf);
					if(fname.length())
					{	if(fname.back()=='\n') fname.erase(fname.length()-1);
						validFilenames[i].push_back(fname);
					}
				}
				pclose(pp);
			}
		}
	}
	
	//Search existing species first:
	for(auto sp: e.iInfo.species)
		if(sp->name == id)
			return sp;
	
	//Search wildcards in order:
	for(size_t i=0; i<validFilenames.size(); i++)
		for(const string& prefix: prefixes)
		{	string pattern = prefix + e.iInfo.pspFilenamePatterns[i];
			pattern.replace(pattern.find("$ID"),3, id);
			for(const string& fname: validFilenames[i])
				if(fname == pattern)
				{	CommandIonSpecies::addSpecies(fname, e, true);
					return e.iInfo.species.back();
				}
		}
	
	return 0; //not found
}


struct CommandChargeball : public Command
{
	CommandChargeball() : Command("chargeball")
	{
		format = "<species-id> <norm> <width>";
		comments =
			"Gaussian chargeball of norm <norm> and width <width> for species <id>\n"
			"This feature is deprecated; when possible, use a pseudopotential with\n"
			"partial core correction instead.";
		allowMultiple = true;

		require("ion-species");
	}

	void process(ParamList& pl, Everything& e)
	{	//Find species:
		string id; pl.get(id, string(), "species-id", true);
		auto sp = findSpecies(id, e);
		if(!sp) throw string("Species "+id+" has not been defined");
		if(sp->Z_chargeball) throw string("chargeball defined multiple times for species "+id);
		//Read parameters:
		pl.get(sp->Z_chargeball, 0., "norm", true);
		pl.get(sp->width_chargeball, 0., "width", true);
	}

	void printStatus(Everything& e, int iRep)
	{	int iCB = 0;
		for(auto sp: e.iInfo.species)
			if(sp->Z_chargeball)
			{	if(iCB == iRep)
				{	logPrintf("%s %lg %lg", sp->name.c_str(), sp->Z_chargeball, sp->width_chargeball);
					break;
				}
				iCB++;
			}
	}
}
commandChargeball;


struct CommandTauCore : public Command
{
	CommandTauCore() : Command("tau-core")
	{
		format = "<species-id> [<rCut>=0] [<plot>=yes|no]";
		comments =
			"Control generation of kinetic energy core correction for species <id>.\n"
			"The core KE density is set to the Thomas-Fermi + von-Weisacker functional\n"
			"of the core electron density (if any), and is pseudized inside within <rCut>\n"
			"If <rCut>=0, it is chosen to be 1.5 times the location of the first radial\n"
			"maximum in the TF+VW KE density. Optionally, if <plot>=yes, the resulting\n"
			"core KE density (and electron density) are output to a gnuplot-friendly file.";
		allowMultiple = true;

		require("ion-species");
	}

	void process(ParamList& pl, Everything& e)
	{	//Find species:
		string id; pl.get(id, string(), "species-id", true);
		auto sp = findSpecies(id, e);
		if(!sp) throw string("Species "+id+" has not been defined");
		//Read parameters:
		pl.get(sp->tauCore_rCut, 0., "rCut", true);
		pl.get(sp->tauCorePlot, false, boolMap, "plot");
	}

	void printStatus(Everything& e, int iRep)
	{	if(unsigned(iRep) < e.iInfo.species.size())
		{	const SpeciesInfo& sp = *(e.iInfo.species[iRep]);
			logPrintf("%s %lg %s", sp.name.c_str(), sp.tauCore_rCut, boolMap.getString(sp.tauCorePlot));
		}
	}
}
commandTauCore;


struct CommandSetVDW : public Command
{
	CommandSetVDW() : Command("setVDW")
	{	format = "<species> <C6> <R0> [ <species2> ... ]";
		comments =
			"Manually adjust DFT-D2 vdW parameters from the default (atomic number based) values.\n"
			"Specify C6 in J/mol*Angstrom^6 and R0 in Angstrom.";
		
		require("ion");
	}
	
	void process(ParamList& pl, Everything& e)
	{	string id;
		pl.get(id, string(), "species", true);
		while(id.length())
		{	auto sp = findSpecies(id, e);
			if(sp)
			{	double C6; double R0;
				pl.get(C6, 0.0, "C6", true);
				pl.get(R0, 0.0, "R0", true);
				sp->vdwOverride = std::make_shared<VanDerWaals::AtomParams>(C6,R0); //note constructor does conversions to a.u
			}
			else throw string("Species "+id+" has not been defined");
			//Check for additional species:
			pl.get(id, string(), "species");
		}
	}
	
	void printStatus(Everything& e, int iRep)
	{	bool first = true;
		for(auto sp: e.iInfo.species)
			if(sp->vdwOverride)
			{	if(!first) logPrintf(" \\\n"); first = false;
				logPrintf("\t%s %lg %lg", sp->name.c_str(),
					sp->vdwOverride->C6 / (Joule*pow(1e-9*meter,6)/mol),
					sp->vdwOverride->R0 / Angstrom); 
			}
	}
}
commandSetVDW;


struct CommandAddU : public Command
{
	CommandAddU() : Command("add-U")
	{	format = "<species> <orbDesc> <UminusJ> [Vext <atom> <V>] ... [ <species2> ... ]";
		comments =
			"Add U correction (DFT+U) to specified species and orbitals, in the simplified\n"
			"rotationally-invariant scheme of [Dudarev et al, Phys. Rev. B 57, 1505], where\n"
			"the correction depends only on U - J.\n"
			"+ <species> is a species identifier (see command ion-species)\n"
			"+ <orbDesc> is one of s,p,d or f.\n"
			"+ <UminusJ> = U-J is the on-site correction energy in hartrees.\n"
			"+ Vext <atom> <V>: optionally specify an external potential on the atomic projection\n"
			"   which may be used to calculate U from linear response. <atom> is the atom\n"
			"   number of this species (1-based) to perturb by strength <V> (in Eh). Multiple\n"
			"   Vext's may appear per U channel to perturb multiple atoms simultaneously.\n"
			"\n"
			"Repeat the sequence for corrections to multiple species.\n"
			"If pseudoatom has multiple shells of same angular momentum, prefix <orbDesc>\n"
			"with a number e.g. 1p or 2p to select the first or second p shell respectively.";
		
		require("ion");
	}
	
	void process(ParamList& pl, Everything& e)
	{	e.eInfo.hasU = false;
		string id;
		pl.get(id, string(), "species", true);
		SpeciesInfo::PlusU* plusUprev = 0;
		while(id.length())
		{	auto sp = findSpecies(id, e);
			if(sp)
			{	SpeciesInfo::PlusU plusU;
				//Get the orbital description:
				string orbCode; pl.get(orbCode, string(), "orbDesc", true);
				size_t lPos = string("spdf").find_first_of(orbCode.back());
				if(lPos==string::npos) throw  "'" + orbCode + "' is not a valid orbital code.";
				plusU.l = int(lPos);
				plusU.n = 0;
				if(orbCode.length() > 1)
				{	plusU.n = atoi(orbCode.substr(0, orbCode.length()-1).c_str()) - 1;
					if(plusU.n<0) throw string("Principal quantum number in orbital description must be a positive integer");
				}
				//Get the value:
				pl.get(plusU.UminusJ, 0., "UminusJ", true);
				//Add U descriptor to species:
				sp->plusU.push_back(plusU);
				e.eInfo.hasU = true;
				//Prepare for a possible Vext:
				plusUprev = &sp->plusU.back();
				plusUprev->Vext.assign(sp->atpos.size(), 0.);
			}
			else if(id=="Vext" && plusUprev)
			{	size_t atom; double V;
				pl.get(atom, size_t(0), "atom", true);
				pl.get(V, 0., "V", true);
				if(atom<1 || atom>plusUprev->Vext.size())
					throw string("Atom number for Vext must be >= 1 and <= nAtoms");
				plusUprev->Vext[atom-1] = V;
			}
			else throw string("Species "+id+" has not been defined");
			//Check for additional species:
			pl.get(id, string(), "species");
		}
		Citations::add("Simplified rotationally-invariant DFT+U", "S. L. Dudarev et al., Phys. Rev. B 57, 1505 (1998)");
	}
	
	void printStatus(Everything& e, int iRep)
	{	bool first = true;
		for(auto sp: e.iInfo.species)
			for(auto plusU: sp->plusU)
			{	if(!first) logPrintf(" \\\n"); first = false;
				ostringstream oss;
				if(plusU.n) oss << (plusU.n + 1);
				oss << string("spdf")[plusU.l];
				logPrintf("\t%s %s %lg", sp->name.c_str(), oss.str().c_str(), plusU.UminusJ);
				for(size_t atom=1; atom<=plusU.Vext.size(); atom++)
					if(plusU.Vext[atom-1]) logPrintf("  Vext %lu %lg", atom, plusU.Vext[atom-1]);
			}
	}
}
commandAddU;
