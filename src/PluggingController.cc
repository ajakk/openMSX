// $Id$

#include <cassert>
#include <cstdio>
#include "CommandController.hh"
#include "MSXCPU.hh"
#include "PluggingController.hh"
#include "Connector.hh"
#include "Pluggable.hh"
#include "PluggableFactory.hh"
#include "openmsx.hh"


namespace openmsx {

PluggingController::PluggingController()
{
	PluggableFactory::createAll(this);

	CommandController::instance()->registerCommand(&plugCmd,   "plug");
	CommandController::instance()->registerCommand(&unplugCmd, "unplug");
}

PluggingController::~PluggingController()
{
	CommandController::instance()->unregisterCommand(&plugCmd,   "plug");
	CommandController::instance()->unregisterCommand(&unplugCmd, "unplug");

#ifndef NDEBUG
	// This is similar to an assert: it should never print anything,
	// but if it does, it helps to catch an error.
	for (vector<Connector *>::const_iterator it = connectors.begin();
		it != connectors.end(); ++it
	) {
		fprintf(stderr,
			"ERROR: Connector still plugged at shutdown: %s\n",
			(*it)->getName().c_str()
			);
	}
#endif
	for (vector<Pluggable *>::iterator it = pluggables.begin();
		it != pluggables.end(); ++it
	) {
		delete (*it);
	}
}

PluggingController *PluggingController::instance()
{
	static PluggingController oneInstance;
	return &oneInstance;
}

void PluggingController::registerConnector(Connector *connector)
{
	connectors.push_back(connector);
}

void PluggingController::unregisterConnector(Connector *connector)
{
	for (vector<Connector *>::iterator it = connectors.begin();
	     it != connectors.end();
	     ++it) {
		if ((*it) == connector) {
			connectors.erase(it);
			return;
		}
	}
}


void PluggingController::registerPluggable(Pluggable *pluggable)
{
	pluggables.push_back(pluggable);
}

void PluggingController::unregisterPluggable(Pluggable *pluggable)
{
	for (vector<Pluggable *>::iterator it = pluggables.begin();
	     it != pluggables.end();
	     ++it) {
		if ((*it) == pluggable) {
			pluggables.erase(it);
			return;
		}
	}
}

// === Commands ===
//  plug command

void PluggingController::PlugCmd::execute(const vector<string> &tokens)
{
	const EmuTime &time = MSXCPU::instance()->getCurrentTime();
	PluggingController *controller = PluggingController::instance();
	switch (tokens.size()) {
		case 1: {
			for (vector<Connector *>::const_iterator it =
			                       controller->connectors.begin();
			     it != controller->connectors.end();
			     ++it) {
				print((*it)->getName() + ": " +
				      (*it)->getPlugged()->getName());
			}
			break;
		}
		case 2: {
			Connector *connector = controller->getConnector(tokens[1]);
			if (connector == NULL) {
				throw CommandException("plug: " + tokens[1] + ": no such connector");
			}
			print(connector->getName() + ": " +
			      connector->getPlugged()->getName());
			break;
		}
		case 3: {
			Connector *connector = controller->getConnector(tokens[1]);
			if (connector == NULL) {
				throw CommandException("plug: " + tokens[1] + ": no such connector");
			}
			Pluggable *pluggable = controller->getPluggable(tokens[2]);
			if (pluggable == NULL) {
				throw CommandException("plug: " + tokens[2] + ": no such pluggable");
			}
			if (connector->getClass() != pluggable->getClass()) {
				throw CommandException("plug: " + tokens[2] + " doesn't fit in " + tokens[1]);
			}
			connector->unplug(time);
			try {
				connector->plug(pluggable, time);
			} catch (PlugException &e) {
				throw CommandException("plug: plug failed: " + e.getMessage());
			}
			break;
		}
	default:
		throw CommandException("plug: syntax error");
	}
}

void PluggingController::PlugCmd::help(const vector<string> &tokens) const
{
	print("Plugs a plug into a connector");
	print(" plug [connector] [plug]");
}

void PluggingController::PlugCmd::tabCompletion(vector<string> &tokens) const
{
	PluggingController *controller = PluggingController::instance();
	if (tokens.size() == 2) {
		// complete connector
		set<string> connectors;
		for (vector<Connector*>::const_iterator it =
			               controller->connectors.begin();
		     it != controller->connectors.end();
		     ++it) {
			connectors.insert((*it)->getName());
		}
		CommandController::completeString(tokens, connectors);
	} else if (tokens.size() == 3) {
		// complete pluggable
		set<string> pluggables;
		Connector* connector = controller->getConnector(tokens[1]);
		string className = connector ? connector->getClass() : "";
		for (vector<Pluggable*>::const_iterator it =
			 controller->pluggables.begin();
		     it != controller->pluggables.end();
		     ++it) {
			Pluggable* pluggable = *it;
			if (pluggable->getClass() == className) {
				pluggables.insert(pluggable->getName());
			}
		}
		CommandController::completeString(tokens, pluggables);
	}
}


//  unplug command

void PluggingController::UnplugCmd::execute(const vector<string> &tokens)
{
	if (tokens.size() != 2) {
		throw CommandException("Syntax error");
	}
	PluggingController *controller = PluggingController::instance();
	Connector *connector = controller->getConnector(tokens[1]);
	if (connector == NULL) {
		throw CommandException("No such connector");
	}
	const EmuTime &time = MSXCPU::instance()->getCurrentTime();
	connector->unplug(time);
}

void PluggingController::UnplugCmd::help(const vector<string> &tokens) const
{
	print("Unplugs a plug from a connector");
	print(" unplug [connector]");
}

void PluggingController::UnplugCmd::tabCompletion(vector<string> &tokens) const
{
	PluggingController *controller = PluggingController::instance();
	if (tokens.size() == 2) {
		// complete connector
		set<string> connectors;
		for (vector<Connector *>::const_iterator it =
		                       controller->connectors.begin();
		     it != controller->connectors.end();
		     ++it) {
			connectors.insert((*it)->getName());
		}
		CommandController::completeString(tokens, connectors);
	}
}

Connector *PluggingController::getConnector(const string& name)
{
	for (vector<Connector *>::const_iterator it = connectors.begin();
	     it != connectors.end();
	     ++it) {
		if ((*it)->getName() == name) {
			return *it;
		}
	}
	return NULL;
}

Pluggable *PluggingController::getPluggable(const string& name)
{
	for (vector<Pluggable *>::const_iterator it = pluggables.begin();
	     it != pluggables.end();
	     ++it) {
		if ((*it)->getName() == name) {
			return *it;
		}
	}
	return NULL;
}

} // namespace openmsx
